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

// cmodel.c -- model loading
#include "cm_local.h"
#include "cm_chunk.h"
#ifdef USE_INTERNAL_ZLIB
#include "zlib/zlib.h"
#else
#include <zlib.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include "qcommon/qfiles.h"

#ifdef BSPC

#include "../bspc/l_qfiles.h"

void SetPlaneSignbits (cplane_t *out) {
	int	bits, j;

	// for fast box on planeside test
	bits = 0;
	for (j=0 ; j<3 ; j++) {
		if (out->normal[j] < 0) {
			bits |= 1<<j;
		}
	}
	out->signbits = bits;
}
#endif //BSPC

// to allow boxes to be treated as brush models, we allocate
// some extra indexes along with those needed by the map
#define	BOX_BRUSHES		1
#define	BOX_SIDES		6
#define	BOX_LEAFS		2
#define	BOX_PLANES		12

#define	LL(x) x=LittleLong(x)


clipMap_t	cmg; //rwwRMG - changed from cm
int			c_pointcontents;
int			c_traces, c_brush_traces, c_patch_traces;


// thread-local: residency's worker parses a collision chunk while the main thread may load another BSP
thread_local byte	*cmod_base;

#ifndef BSPC
cvar_t		*cm_noAreas;
cvar_t		*cm_noCurves;
cvar_t		*cm_playerCurveClip;
cvar_t		*cm_extraVerbose;
#endif

cmodel_t	box_model;
cmPlane_t	*box_planes;		// the box hull's twelve (CM_InitBoxHull), past the map's planes
byte		cm_boxPlaneType[12];	// their types: 0-2 for the positive normals, 3-5 for the negative (CM_LoadPlane)
cbrush_t	*box_brush;



void	CM_InitBoxHull (void);
void	CM_FloodAreaConnections (clipMap_t &cm);

//rwwRMG - added:
clipMap_t	SubBSP[MAX_SUB_BSP];
int			NumSubBSP, TotalSubModels;

static thread_local qboolean	cm_loadingChunk;	// a chunk's model 0 keeps its tree and adds no inline models
static thread_local cmChunk_t	*cm_loadingTarget;	// the chunk whose blocks CM_Alloc records

/*
==================
CM_Alloc

Hunk_Alloc for the map; for a collision chunk (CM_ParseChunk, on either thread), malloc'd blocks of its own, recorded so
that residency can free that chunk alone. A chunk is placed after the map, past the hunk's mark, so on the hunk a
listen server's client clearing to the mark would free it under the traces
==================
*/
struct cmParseFail {						// a chunk's parse that cannot go on (CM_ParseChunk catches it)
	const char	*why;
};

static void *CM_Alloc( size_t size, ha_pref preference ) {
	if ( !( cm_loadingChunk && cm_loadingTarget ) ) {
		if ( size > 0x7FFFFFFF ) {
			Com_Error( ERR_DROP, "CM_Alloc: %llu bytes for one array", (unsigned long long)size );
		}
		return Hunk_Alloc( (int)size, preference );
	}
	if ( cm_loadingTarget->numAllocs == MAX_CM_CHUNK_ALLOCS ) {
		throw cmParseFail{ "more blocks than a chunk has" };
	}
	void *p = calloc( 1, size );
	if ( !p ) {
		throw cmParseFail{ "out of memory" };	// a NULL written through would end the process
	}
	cm_loadingTarget->allocs[cm_loadingTarget->numAllocs++] = p;
	cm_loadingTarget->bytes += size;
	return p;
}

/*
===============================================================================

					MAP LOADING

===============================================================================
*/

/*
=================
CMod_LoadShaders
=================
*/
static void CMod_LoadShaders( const lump_t *l, clipMap_t &cm )
{
	dshader_t	*in;
	int			i, count;
	CCMShader	*out;

	in = (dshader_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) {
		Com_Error (ERR_DROP, "CMod_LoadShaders: funny lump size");
	}
	count = l->filelen / sizeof(*in);

	if (count < 1) {
		Com_Error (ERR_DROP, "Map with no shaders");
	}
	cm.shaders = (CCMShader *)CM_Alloc( (1+count) * sizeof( *cm.shaders ), h_high );
	cm.numShaders = count;

	out = cm.shaders;
	for ( i = 0; i < count; i++, in++, out++ )
	{
		Q_strncpyz(out->shader, in->shader, MAX_QPATH);
		out->contentFlags = LittleLong( in->contentFlags );
		out->surfaceFlags = LittleLong( in->surfaceFlags );
	}
}


/*
=================
CMod_LoadSubmodels
=================
*/
static void CMod_LoadSubmodels( const lump_t *l, clipMap_t &cm ) {
	dmodel_t	*in;
	cmodel_t	*out;
	int			i, j, count;
	int			*indexes;

	in = (dmodel_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadSubmodels: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no models");
	cm.cmodels = (struct cmodel_s *)CM_Alloc( count * sizeof( *cm.cmodels ), h_high );
	cm.numSubModels = count;

	if ( count > MAX_SUBMODELS ) {
		Com_Error( ERR_DROP, "MAX_SUBMODELS exceeded" );
	}

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out = &cm.cmodels[i];

		for (j=0 ; j<3 ; j++)
		{	// spread the mins / maxs by a pixel
			out->mins[j] = LittleFloat (in->mins[j]) - 1;
			out->maxs[j] = LittleFloat (in->maxs[j]) + 1;
		}

		//rwwRMG - sof2 doesn't have to add this &cm == &cmg check.
		//Are they getting leaf data elsewhere? (the reason this needs to be done is
		//in sub bsp instances the first brush model isn't necessary a world model and might be
		//real architecture)
		if ( i == 0 && ( &cm == &cmg || cm_loadingChunk ) ) {
			out->firstNode = 0;
			continue;	// world model doesn't need other info
		}

		// make a "leaf" just to hold the model's brushes and surfaces
		out->firstNode = -1;

		// make a "leaf" just to hold the model's brushes and surfaces
		out->leaf.numLeafBrushes = LittleLong( in->numBrushes );
		indexes = (int *)CM_Alloc( out->leaf.numLeafBrushes * 4, h_high );
		out->leaf.firstLeafBrush = (int)( indexes - cm.leafbrushes );
		for ( j = 0 ; j < out->leaf.numLeafBrushes ; j++ ) {
			indexes[j] = LittleLong( in->firstBrush ) + j;
		}

		out->leaf.numLeafSurfaces = LittleLong( in->numSurfaces );
		indexes = (int *)CM_Alloc( out->leaf.numLeafSurfaces * 4, h_high );
		out->leaf.firstLeafSurface = (int)( indexes - cm.leafsurfaces );
		for ( j = 0 ; j < out->leaf.numLeafSurfaces ; j++ ) {
			indexes[j] = LittleLong( in->firstSurface ) + j;
		}
	}
}


/*
=================
CMod_LoadNodes

=================
*/
static void CMod_LoadNodes( const lump_t *l, clipMap_t &cm ) {
	dnode_t		*in;
	int			child;
	cNode_t		*out;
	int			i, j, count;

	in = (dnode_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadNodes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map has no nodes");
	cm.nodes = (cNode_t *)CM_Alloc( count * sizeof( *cm.nodes ), h_high );
	cm.numNodes = count;

	out = cm.nodes;

	for (i=0 ; i<count ; i++, out++, in++)
	{
		out->planeNum = LittleLong( in->planeNum );
		for (j=0 ; j<2 ; j++)
		{
			child = LittleLong (in->children[j]);
			out->children[j] = child;
		}
		cplane_t pl;
		CM_LoadPlane( &cm, out->planeNum, &pl );
		out->planeType = pl.type;
	}

}

/*
=================
CM_NextCheckCount

cbrush_t.checkcount is 16 bits against the clip map's int counter. Masking the compare would skip a brush last
tested exactly 65,536 traces earlier (a missed collision), so at the wrap the marks are cleared instead: one pass over
the clip map's brushes and patches every 65,535 traces.
=================
*/
void CM_NextCheckCount( clipMap_t *cm ) {
	if ( ++cm->checkcount <= 0xFFFF ) {
		return;
	}
	for ( int i = 0 ; i < cm->numBrushes ; i++ ) {
		cm->brushes[i].checkcount = 0;
	}
	for ( int i = 0 ; i < cm->numSurfaces ; i++ ) {
		if ( cm->surfaces && cm->surfaces[i] ) {
			cm->surfaces[i]->checkcount = 0;
		}
	}
	cm->checkcount = 1;
}

/*
=================
CM_AxialPlane / CM_SideShader

An implicit brush's six axial planes are built from its bounds, bit for bit the planes the loader read them from:
the normals are exactly the axes (checked at load), dist is the bound (side 0, 2, 4: its negation, exact), and type
and signbits come from the loader's own rules (CMod_LoadPlanes). docs/levelport-collision.md section 31.
=================
*/
static cplane_t cm_axialTemplate[6];
static bool cm_axialReady;
const int cm_axialSignbits[6] = { 1, 0, 2, 0, 4, 0 };	// -x +x -y +y -z +z: the template's (checked when it is built)

static void CM_InitAxialTemplate( void ) {
	for ( int i = 0 ; i < 6 ; i++ ) {
		cplane_t *p = &cm_axialTemplate[i];
		Com_Memset( p, 0, sizeof( *p ) );
		p->normal[i >> 1] = ( i & 1 ) ? 1.0f : -1.0f;
		int bits = 0;
		for ( int j = 0 ; j < 3 ; j++ ) {
			if ( p->normal[j] < 0 ) {
				bits |= 1 << j;
			}
		}
		p->type = PlaneTypeForNormal( p->normal );
		p->signbits = bits;
		if ( bits != cm_axialSignbits[i] ) {
			Com_Error( ERR_FATAL, "CM_InitAxialTemplate: side %d signbits %d", i, bits );
		}
	}
	cm_axialReady = true;
}

void CM_AxialPlane( const cbrush_t *b, int i, cmPlane_t *out ) {
	VectorCopy( cm_axialTemplate[i].normal, out->normal );
	out->dist = ( i & 1 ) ? b->bounds[1][i >> 1] : -b->bounds[0][i >> 1];
}

void CM_SidePlaneFull( const clipMap_t *cm, const cbrush_t *b, int i, cplane_t *out ) {
	if ( b->sideMask & CM_BRUSH_IMPLICIT ) {
		if ( i < 6 ) {
			*out = cm_axialTemplate[i];
			out->dist = ( i & 1 ) ? b->bounds[1][i >> 1] : -b->bounds[0][i >> 1];
		} else {
			CM_LoadPlane( cm, cm->brushsides[b->firstSide + i - 6].planeNum & CM_SIDE_PLANE, out );
		}
	} else {
		CM_LoadPlane( cm, cm->brushsides[b->firstSide + i].planeNum & CM_SIDE_PLANE, out );
	}
}

int CM_SideShader( const clipMap_t *cm, const cbrush_t *b, int i ) {
	const bool implicit = ( b->sideMask & CM_BRUSH_IMPLICIT ) != 0;
	if ( implicit && i < 6 ) {
		return ( b->sideMask & ( 1 << i ) ) ? b->shaderNum : cm->bevelShader;
	}
	const int stored = b->firstSide + i - ( implicit ? 6 : 0 );
	if ( cm->sideShaders ) {
		return cm->sideShaders[stored];
	}
	return ( cm->brushsides[stored].planeNum & CM_SIDE_BEVEL ) ? cm->bevelShader : b->shaderNum;
}

/*
=================
CMod_LoadBrushes

The brushes and their sides (one pass over both lumps). A side keeps its plane alone; its shader is derived when the
map follows the rule (every side its brush's shader or the bevel's), else kept in sideShaders. A brush whose first six
sides are the exact axial planes at its bounds, and whose sides all follow the rule, stores only the rest
(CM_BRUSH_IMPLICIT). More than 65,534 shaders or 65,535 sides to a brush is an error (the records are 16 bits).
=================
*/
void CMod_LoadBrushes( const lump_t *l, const lump_t *sl, clipMap_t &cm ) {
	dbrush_t		*in;
	dbrushside_t	*sin;
	cbrush_t		*out;
	int				i, k, count, sideCount;

	if ( !cm_axialReady ) {
		CM_InitAxialTemplate();
	}
	in = (dbrush_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in)) {
		Com_Error (ERR_DROP, "CMod_LoadBrushes: funny lump size");
	}
	count = l->filelen / sizeof(*in);
	sin = (dbrushside_t *)(cmod_base + sl->fileofs);
	if ( sl->filelen % sizeof(*sin) ) {
		Com_Error (ERR_DROP, "CMod_LoadBrushSides: funny lump size");
	}
	sideCount = sl->filelen / sizeof(*sin);
	if ( cm.numShaders > 65534 ) {
		Com_Error( ERR_DROP, "CMod_LoadBrushes: %i shaders (65,534 at most)", cm.numShaders );
	}
	for ( i = 0 ; i < sideCount ; i++ ) {
		const int sh = LittleLong( sin[i].shaderNum );
		if ( sh < 0 || sh >= cm.numShaders ) {
			Com_Error( ERR_DROP, "CMod_LoadBrushSides: bad shaderNum: %i", sh );
		}
	}
	cm.bevelShader = -1;
	for ( i = 0 ; i < cm.numShaders ; i++ ) {
		if ( !Q_stricmp( cm.shaders[i].shader, "noshader" ) ) {
			cm.bevelShader = i;
			break;
		}
	}

	cm.brushes = (cbrush_t *)CM_Alloc( ( BOX_BRUSHES + count ) * sizeof( *cm.brushes ), h_high );
	cm.numBrushes = count;

	// pass 1: the brushes, their bounds (from the lump's first six sides, as ever), which follow the rule
	// and which are implicit; the stored side count
	bool ruled = true;
	int stored = 0;
	for ( i = 0, out = cm.brushes ; i < count ; i++, out++ ) {
		const int first = LittleLong( in[i].firstSide ), n = LittleLong( in[i].numSides );
		if ( n < 0 || n > 65535 ) {
			Com_Error( ERR_DROP, "CMod_LoadBrushes: brush %i has %i sides", i, n );
		}
		const int sh = LittleLong( in[i].shaderNum );
		if ( sh < 0 || sh >= cm.numShaders ) {
			Com_Error( ERR_DROP, "CMod_LoadBrushes: bad shaderNum: %i", sh );
		}
		out->shaderNum = sh;
		out->numsides = n;
		out->contents = cm.shaders[sh].contentFlags;
		out->firstSide = first;		// the lump's, until pass 2
		cplane_t pl[6];
		for ( k = 0 ; k < 6 ; k++ ) {
			// the bounds were always read from sides[0..5] whatever the count: past the lump, the zeroed box region
			const int s = first + k;
			CM_LoadPlane( &cm, s >= 0 && s < sideCount ? LittleLong( sin[s].planeNum ) : 0, &pl[k] );
		}
		out->bounds[0][0] = -pl[0].dist;
		out->bounds[1][0] = pl[1].dist;
		out->bounds[0][1] = -pl[2].dist;
		out->bounds[1][1] = pl[3].dist;
		out->bounds[0][2] = -pl[4].dist;
		out->bounds[1][2] = pl[5].dist;
		bool brushRuled = true;
		unsigned short mask = 0;
		for ( k = 0 ; k < n ; k++ ) {
			const int ssh = LittleLong( sin[first + k].shaderNum );
			if ( ssh == sh ) {
				if ( k < 6 ) {
					mask |= 1 << k;
				}
			} else if ( ssh != cm.bevelShader ) {
				brushRuled = false;
			}
		}
		ruled = ruled && brushRuled;
		bool axial = n >= 6 && brushRuled;
		for ( k = 0 ; axial && k < 6 ; k++ ) {
			cmPlane_t want;
			CM_AxialPlane( out, k, &want );		// the template's normal: the axis, exact bits
			if ( memcmp( pl[k].normal, want.normal, sizeof( want.normal ) ) ) {
				axial = false;
			}
		}
		out->sideMask = axial ? (unsigned short)( CM_BRUSH_IMPLICIT | mask ) : 0;
		stored += axial ? n - 6 : n;
	}

	// pass 2: the stored sides, each brush's own run (a range two brushes shared in the lump is stored twice)
	cm.brushsides = (cbrushside_t *)CM_Alloc( ( BOX_SIDES + stored ) * sizeof( *cm.brushsides ), h_high );
	cm.numBrushSides = stored;
	cm.sideShaders = ruled ? NULL : (int *)CM_Alloc( ( BOX_SIDES + stored ) * sizeof( *cm.sideShaders ), h_high );
	int next = 0;
	for ( i = 0, out = cm.brushes ; i < count ; i++, out++ ) {
		const int first = out->firstSide, skip = ( out->sideMask & CM_BRUSH_IMPLICIT ) ? 6 : 0;
		out->firstSide = next;
		for ( k = skip ; k < out->numsides ; k++, next++ ) {
			const int ssh = LittleLong( sin[first + k].shaderNum );
			int pn = LittleLong( sin[first + k].planeNum );
			pn |= CM_PlaneSignbits( &cm.planes[pn] ) << CM_SIDE_SIGNS;
			if ( ruled && ssh != out->shaderNum ) {
				pn |= CM_SIDE_BEVEL;
			}
			cm.brushsides[next].planeNum = pn;
			if ( cm.sideShaders ) {
				cm.sideShaders[next] = ssh;
			}
		}
	}
}

/*
=================
CMod_LoadLeafs
=================
*/
static void CMod_LoadLeafs (const lump_t *l, clipMap_t &cm)
{
	int			i;
	cLeaf_t		*out;
	dleaf_t 	*in;
	int			count;

	in = (dleaf_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadLeafs: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no leafs");

	cm.leafs = (cLeaf_t *)CM_Alloc( ( BOX_LEAFS + count ) * sizeof( *cm.leafs ), h_high );
	cm.numLeafs = count;

	out = cm.leafs;
	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->cluster = LittleLong (in->cluster);
		out->area = LittleLong (in->area);
		out->firstLeafBrush = LittleLong (in->firstLeafBrush);
		out->numLeafBrushes = LittleLong (in->numLeafBrushes);
		out->firstLeafSurface = LittleLong (in->firstLeafSurface);
		out->numLeafSurfaces = LittleLong (in->numLeafSurfaces);

		if (out->cluster >= cm.numClusters)
			cm.numClusters = out->cluster + 1;
		if (out->area >= cm.numAreas)
			cm.numAreas = out->area + 1;
	}

	cm.areas = (cArea_t *)CM_Alloc( cm.numAreas * sizeof( *cm.areas ), h_high );
	cm.areaPortals = (int *)CM_Alloc( cm.numAreas * cm.numAreas * sizeof( *cm.areaPortals ), h_high );
}

/*
=================
CMod_LoadPlanes
=================
*/
static void CMod_LoadPlanes (const lump_t *l, clipMap_t &cm)
{
	int			i, j;
	cmPlane_t	*out;
	dplane_t 	*in;
	int			count;

	in = (dplane_t *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadPlanes: funny lump size");
	count = l->filelen / sizeof(*in);

	if (count < 1)
		Com_Error (ERR_DROP, "Map with no planes");
	if ( count > CM_SIDE_PLANE + 1 - BOX_PLANES )
		Com_Error (ERR_DROP, "CMod_LoadPlanes: %i planes (a side holds 27 bits of plane)", count);
	cm.planes = (cmPlane_t *)CM_Alloc( ( BOX_PLANES + count ) * sizeof( *cm.planes ), h_high );
	cm.numPlanes = count;

	out = cm.planes;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
		}
		out->dist = LittleFloat (in->dist);		// type and signbits: CM_LoadPlane
	}
}

/*
=================
CMod_LoadLeafBrushes
=================
*/
static void CMod_LoadLeafBrushes (const lump_t *l, clipMap_t &cm)
{
	int			i;
	int			*out;
	int		 	*in;
	int			count;

	in = (int *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadLeafBrushes: funny lump size");
	count = l->filelen / sizeof(*in);

	cm.leafbrushes = (int *)CM_Alloc( (count + BOX_BRUSHES) * sizeof( *cm.leafbrushes ), h_high );
	cm.numLeafBrushes = count;

	out = cm.leafbrushes;

	for ( i=0 ; i<count ; i++, in++, out++) {
		*out = LittleLong (*in);
	}
}

/*
=================
CMod_LoadLeafSurfaces
=================
*/
static void CMod_LoadLeafSurfaces( const lump_t *l, clipMap_t &cm )
{
	int			i;
	int			*out;
	int		 	*in;
	int			count;

	in = (int *)(cmod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "CMod_LoadLeafSurfaces: funny lump size");
	count = l->filelen / sizeof(*in);

	cm.leafsurfaces = (int *)CM_Alloc( count * sizeof( *cm.leafsurfaces ), h_high );
	cm.numLeafSurfaces = count;

	out = cm.leafsurfaces;

	for ( i=0 ; i<count ; i++, in++, out++) {
		*out = LittleLong (*in);
	}
}


/*
=================
CMod_LoadEntityString
=================
*/
static void CMod_LoadEntityString( const lump_t *l, clipMap_t &cm, const char* name ) {
	fileHandle_t h;
	char entName[MAX_QPATH];

	// Attempt to load entities from an external .ent file if available
	Q_strncpyz(entName, name, sizeof(entName));
	const size_t entNameLen = strlen(entName);
	entName[entNameLen - 3] = 'e';
	entName[entNameLen - 2] = 'n';
	entName[entNameLen - 1] = 't';
	const int iEntityFileLen = FS_FOpenFileRead(entName, &h, qfalse);
	if (h)
	{
		cm.entityString = (char *)CM_Alloc(iEntityFileLen + 1, h_high);
		cm.numEntityChars = iEntityFileLen + 1;
		FS_Read(cm.entityString, iEntityFileLen, h);
		FS_FCloseFile(h);
		cm.entityString[iEntityFileLen] = '\0';
		Com_Printf("Loaded entities from %s\n", entName);
		return;
	}

	cm.entityString = (char *)CM_Alloc( l->filelen, h_high );
	cm.numEntityChars = l->filelen;
	Com_Memcpy (cm.entityString, cmod_base + l->fileofs, l->filelen);
}

/*
=================
CMod_LoadVisibility
=================
*/
#define	VIS_HEADER	8
static void CMod_LoadVisibility( const lump_t *l, clipMap_t &cm ) {
	int		len;
	byte	*buf;

    len = l->filelen;
	if ( !len ) {
		cm.clusterBytes = ( cm.numClusters + 31 ) & ~31;
		cm.visibility = (unsigned char *)CM_Alloc( cm.clusterBytes, h_high );
		Com_Memset( cm.visibility, 255, cm.clusterBytes );
		return;
	}
	buf = cmod_base + l->fileofs;

	cm.vised = qtrue;
	cm.visibility = (unsigned char *)CM_Alloc( len, h_high );
	cm.numClusters = LittleLong( ((int *)buf)[0] );
	cm.clusterBytes = LittleLong( ((int *)buf)[1] );
	Com_Memcpy (cm.visibility, buf + VIS_HEADER, len - VIS_HEADER );
}

//==================================================================


/*
=================
CMod_LoadPatches
=================
*/
#define	MAX_PATCH_VERTS		1024
static void CMod_LoadPatches( const lump_t *surfs, const lump_t *verts, clipMap_t &cm ) {
	drawVert_t	*dv, *dv_p;
	dsurface_t	*in;
	int			count;
	int			i, j;
	int			c;
	cPatch_t	*patch;
	vec3_t		points[MAX_PATCH_VERTS];
	int			width, height;
	int			shaderNum;

	in = (dsurface_t *)(cmod_base + surfs->fileofs);
	if (surfs->filelen % sizeof(*in))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size");
	cm.numSurfaces = count = surfs->filelen / sizeof(*in);
	cm.surfaces = (cPatch_t ** )CM_Alloc( cm.numSurfaces * sizeof( cm.surfaces[0] ), h_high );

	dv = (drawVert_t *)(cmod_base + verts->fileofs);
	if (verts->filelen % sizeof(*dv))
		Com_Error (ERR_DROP, "MOD_LoadBmodel: funny lump size");

	// scan through all the surfaces, but only load patches,
	// not planar faces
	for ( i = 0 ; i < count ; i++, in++ ) {
		if ( LittleLong( in->surfaceType ) != MST_PATCH ) {
			continue;		// ignore other surfaces
		}
		// FIXME: check for non-colliding patches

		cm.surfaces[ i ] = patch = (cPatch_t *)CM_Alloc( sizeof( *patch ), h_high );

		// load the full drawverts onto the stack
		width = LittleLong( in->patchWidth );
		height = LittleLong( in->patchHeight );
		c = width * height;
		if ( c > MAX_PATCH_VERTS ) {
			Com_Error( ERR_DROP, "ParseMesh: MAX_PATCH_VERTS" );
		}

		dv_p = dv + LittleLong( in->firstVert );
		for ( j = 0 ; j < c ; j++, dv_p++ ) {
			points[j][0] = LittleFloat( dv_p->xyz[0] );
			points[j][1] = LittleFloat( dv_p->xyz[1] );
			points[j][2] = LittleFloat( dv_p->xyz[2] );
		}

		shaderNum = LittleLong( in->shaderNum );
		patch->contents = cm.shaders[shaderNum].contentFlags;
		patch->surfaceFlags = cm.shaders[shaderNum].surfaceFlags;

		// create the internal facet structure
		patch->pc = CM_GeneratePatchCollide( width, height, points );
	}
}

//==================================================================

/*
==================
CM_LoadMap

Loads in the map and all submodels
==================
*/
void *gpvCachedMapDiskImage = NULL;
char  gsCachedMapDiskImage[MAX_QPATH];
qboolean gbUsingCachedMapDataRightNow = qfalse;	// if true, signifies that you can't delete this at the moment!! (used during z_malloc()-fail recovery attempt)

// called in response to a "devmapbsp blah" or "devmapall blah" command, do NOT use inside CM_Load unless you pass in qtrue
//
// new bool return used to see if anything was freed, used during z_malloc failure re-try
//
qboolean CM_DeleteCachedMap(qboolean bGuaranteedOkToDelete)
{
	qboolean bActuallyFreedSomething = qfalse;

	if (bGuaranteedOkToDelete || !gbUsingCachedMapDataRightNow)
	{
		// dump cached disk image...
		//
		if (gpvCachedMapDiskImage)
		{
			Z_Free(	gpvCachedMapDiskImage );
					gpvCachedMapDiskImage = NULL;

			bActuallyFreedSomething = qtrue;
		}
		gsCachedMapDiskImage[0] = '\0';

		// force map loader to ignore cached internal BSP structures for next level CM_LoadMap() call...
		//
		cmg.name[0] = '\0';
	}

	return bActuallyFreedSomething;
}





static void CM_LoadMap_Actual( const char *name, qboolean clientload, int *checksum, clipMap_t &cm )
{ //rwwRMG - function needs heavy modification
	int				*buf;
	dheader_t		header;
	static unsigned	last_checksum;
	char			origName[MAX_OSPATH];
	void			*newBuff = 0;

	if ( !name || !name[0] ) {
		Com_Error( ERR_DROP, "CM_LoadMap: NULL name" );
	}

#ifndef BSPC
	cm_noAreas = Cvar_Get ("cm_noAreas", "0", CVAR_CHEAT);
	cm_noCurves = Cvar_Get ("cm_noCurves", "0", CVAR_CHEAT);
	cm_playerCurveClip = Cvar_Get ("cm_playerCurveClip", "1", CVAR_ARCHIVE_ND|CVAR_CHEAT );
	cm_extraVerbose = Cvar_Get ("cm_extraVerbose", "0", CVAR_TEMP );
#endif
	Com_DPrintf( "CM_LoadMap( %s, %i )\n", name, clientload );

	if ( !strcmp( cm.name, name ) && clientload ) {
		if ( checksum )
			*checksum = last_checksum;
		return;
	}

	strcpy(origName, name);

	if (&cm == &cmg)
	{
		// free old stuff
		CM_ClearMap();
		CM_ClearLevelPatches();
	}

	// free old stuff
	Com_Memset( &cm, 0, sizeof( cm ) );

	if ( !name[0] ) {
		cm.numLeafs = 1;
		cm.numClusters = 1;
		cm.numAreas = 1;
		cm.cmodels = (struct cmodel_s *)CM_Alloc( sizeof( *cm.cmodels ), h_high );
		if ( checksum )
			*checksum = 0;
		return;
	}

	//
	// load the file
	//
	//rww - Doesn't this sort of defeat the purpose? We're clearing it even if the map is the same as the last one!
	//Not touching it though in case I'm just overlooking something.
	if (gpvCachedMapDiskImage && &cm == &cmg)	// MP code: this'll only be NZ if we got an ERR_DROP during last map load,
	{							//	so it's really just a safety measure.
		Z_Free(	gpvCachedMapDiskImage);
				gpvCachedMapDiskImage = NULL;
	}

#ifndef BSPC
	//
	// load the file into a buffer that we either discard as usual at the bottom, or if we've got enough memory
	//	then keep it long enough to save the renderer re-loading it (if not dedicated server),
	//	then discard it after that...
	//
	buf = NULL;
	fileHandle_t h;
	const int iBSPLen = FS_FOpenFileRead( name, &h, qfalse );
	if (h)
	{
		newBuff = Z_Malloc( iBSPLen, TAG_BSP_DISKIMAGE );
		FS_Read( newBuff, iBSPLen, h);
		FS_FCloseFile( h );

		buf = (int*) newBuff;	// so the rest of the code works as normal
		if (&cm == &cmg)
		{
			gpvCachedMapDiskImage = newBuff;
			newBuff = 0;
		}

		// carry on as before...
		//
	}
#else
	const int iBSPLen = LoadQuakeFile((quakefile_t *) name, (void **)&buf);
#endif

	if ( !buf ) {
		Com_Error (ERR_DROP, "Couldn't load %s", name);
	}

	last_checksum = LittleLong (Com_BlockChecksum (buf, iBSPLen));
	if ( checksum )
		*checksum = last_checksum;

	header = *(dheader_t *)buf;
	for (size_t i=0 ; i<sizeof(dheader_t)/4 ; i++) {
		((int *)&header)[i] = LittleLong ( ((int *)&header)[i]);
	}

	if ( header.version != BSP_VERSION ) {
		Z_Free(	gpvCachedMapDiskImage);
				gpvCachedMapDiskImage = NULL;

		Com_Error (ERR_DROP, "CM_LoadMap: %s has wrong version number (%i should be %i)"
		, name, header.version, BSP_VERSION );
	}

	cmod_base = (byte *)buf;

	// load into heap
	CMod_LoadShaders( &header.lumps[LUMP_SHADERS], cm );
	CMod_LoadLeafs (&header.lumps[LUMP_LEAFS], cm);
	CMod_LoadLeafBrushes (&header.lumps[LUMP_LEAFBRUSHES], cm);
	CMod_LoadLeafSurfaces (&header.lumps[LUMP_LEAFSURFACES], cm);
	CMod_LoadPlanes (&header.lumps[LUMP_PLANES], cm);
	CMod_LoadBrushes (&header.lumps[LUMP_BRUSHES], &header.lumps[LUMP_BRUSHSIDES], cm);
	CMod_LoadSubmodels (&header.lumps[LUMP_MODELS], cm);
	CMod_LoadNodes (&header.lumps[LUMP_NODES], cm);
	CMod_LoadEntityString (&header.lumps[LUMP_ENTITIES], cm, name);
	CMod_LoadVisibility( &header.lumps[LUMP_VISIBILITY], cm );
	CMod_LoadPatches( &header.lumps[LUMP_SURFACES], &header.lumps[LUMP_DRAWVERTS], cm );

	TotalSubModels += cm.numSubModels;

	if (&cm == &cmg)
	{
		// Load in the shader text - return instantly if already loaded
		CM_InitBoxHull ();
	}

#ifndef BSPC	// I hope we can lose this crap soon
	//
	// if we've got enough memory, and it's not a dedicated-server, then keep the loaded map binary around
	//	for the renderer to chew on... (but not if this gets ported to a big-endian machine, because some of the
	//	map data will have been Little-Long'd, but some hasn't).
	//
	// For sub BSPs, always free immediately since caching only applies to main maps
	if ( newBuff && newBuff != gpvCachedMapDiskImage )
	{
		Z_Free(newBuff);
	}
	else if (Sys_LowPhysicalMemory()
		|| com_dedicated->integer
//		|| we're on a big-endian machine
		)
	{
		Z_Free(	gpvCachedMapDiskImage );
				gpvCachedMapDiskImage = NULL;
	}
	else
	{
		// ... do nothing, and let the renderer free it after it's finished playing with it...
		//
	}
#else
	FS_FreeFile (buf);
#endif

	CM_FloodAreaConnections (cm);

	// allow this to be cached if it is loaded by the server
	if ( !clientload ) {
		Q_strncpyz( cm.name, origName, sizeof( cm.name ) );
	}
}



// need a wrapper function around this because of multiple returns, need to ensure bool is correct...
//
void CM_LoadMap( const char *name, qboolean clientload, int *checksum )
{
	// a map's load never inherits a chunk parse's state (the parser restores it on every exit; this is the backstop)
	cm_loadingChunk = qfalse;
	cm_loadingTarget = NULL;
	gbUsingCachedMapDataRightNow = qtrue;	// !!!!!!!!!!!!!!!!!!

		CM_LoadMap_Actual( name, clientload, checksum, cmg );

	gbUsingCachedMapDataRightNow = qfalse;	// !!!!!!!!!!!!!!!!!!
	CM_LoadMapChunks( name, clientload );
}



/*
==================
CM_ClearMap
==================
*/
void CM_ClearMap( void )
{
	int		i;

	Com_Memset( &cmg, 0, sizeof( cmg ) );
	CM_ClearLevelPatches();

	for(i = 0; i < NumSubBSP; i++)
	{
		memset(&SubBSP[i], 0, sizeof(SubBSP[0]));
	}
	NumSubBSP = 0;
	TotalSubModels = 0;
	CM_ClearChunks();
}

/*
==================
CM_ClipHandleToModel
==================
*/
cmodel_t	*CM_ClipHandleToModel( clipHandle_t handle, clipMap_t **clipMap ) {
	int		i;
	int		count;

	if ( handle < 0 )
	{
		Com_Error( ERR_DROP, "CM_ClipHandleToModel: bad handle %i", handle );
	}
	if ( handle < cmg.numSubModels )
	{
		if (clipMap)
		{
			*clipMap = &cmg;
		}
		return &cmg.cmodels[handle];
	}
	if ( handle == BOX_MODEL_HANDLE )
	{
		if (clipMap)
		{
			*clipMap = &cmg;
		}
		return &box_model;
	}

	count = cmg.numSubModels;
	for(i = 0; i < NumSubBSP; i++)
	{
		if (handle < count + SubBSP[i].numSubModels)
		{
			if (clipMap)
			{
				*clipMap = &SubBSP[i];
			}
			return &SubBSP[i].cmodels[handle - count];
		}
		count += SubBSP[i].numSubModels;
	}

	if ( handle < MAX_SUBMODELS )
	{
		Com_Error( ERR_DROP, "CM_ClipHandleToModel: bad handle (count: %i) < (handle: %i) < (max: %i)",
			cmg.numSubModels, handle, MAX_SUBMODELS );
	}
	Com_Error( ERR_DROP, "CM_ClipHandleToModel: bad handle %i", handle + MAX_SUBMODELS );

	return NULL;
}

/*
==================
CM_InlineModel
==================
*/
clipHandle_t	CM_InlineModel( int index ) {
	if ( index < 0 || index >= TotalSubModels ) {
		Com_Error( ERR_DROP, "CM_InlineModel: bad number: %d >= %d (may need to re-BSP map?)", index, TotalSubModels );
	}
	return index;
}

int		CM_NumInlineModels( void ) {
	return cmg.numSubModels;
}

char	*CM_EntityString( void ) {
	return cmg.entityString;
}

char *CM_SubBSPEntityString( int index )
{
	return SubBSP[index].entityString;
}

int		CM_LeafCluster( int leafnum ) {
	if (leafnum < 0 || leafnum >= cmg.numLeafs) {
		Com_Error (ERR_DROP, "CM_LeafCluster: bad number");
	}
	return cmg.leafs[leafnum].cluster;
}

int		CM_LeafArea( int leafnum ) {
	if ( leafnum < 0 || leafnum >= cmg.numLeafs ) {
		Com_Error (ERR_DROP, "CM_LeafArea: bad number");
	}
	return cmg.leafs[leafnum].area;
}

//=======================================================================


/*
===================
CM_InitBoxHull

Set up the planes and nodes so that the six floats of a bounding box
can just be stored out and get a proper clipping hull structure.
===================
*/
void CM_InitBoxHull (void)
{
	int			i;
	int			side;
	cmPlane_t	*p;
	cbrushside_t	*s;

	box_planes = &cmg.planes[cmg.numPlanes];

	box_brush = &cmg.brushes[cmg.numBrushes];
	box_brush->numsides = 6;
	box_brush->firstSide = cmg.numBrushSides;
	box_brush->contents = CONTENTS_BODY;
	box_brush->shaderNum = cmg.numShaders;		// its sides' shader (CM_SideShader): the slot past the map's
	box_brush->sideMask = 0;					// all six stored: their planes move with every box

	box_model.firstNode = -1;
	box_model.leaf.numLeafBrushes = 1;
//	box_model.leaf.firstLeafBrush = cmg.numBrushes;
	box_model.leaf.firstLeafBrush = cmg.numLeafBrushes;
	cmg.leafbrushes[cmg.numLeafBrushes] = cmg.numBrushes;

	for (i=0 ; i<6 ; i++)
	{
		side = i&1;

		// brush sides
		s = &cmg.brushsides[cmg.numBrushSides+i];
		s->planeNum = cmg.numPlanes+i*2+side;
		if ( cmg.sideShaders ) {
			cmg.sideShaders[cmg.numBrushSides+i] = cmg.numShaders;
		}

		// planes (signbits follow from the normals in CM_LoadPlane, as SetPlaneSignbits gave them)
		p = &box_planes[i*2];
		cm_boxPlaneType[i*2] = i>>1;
		VectorClear (p->normal);
		p->normal[i>>1] = 1;

		p = &box_planes[i*2+1];
		cm_boxPlaneType[i*2+1] = 3 + (i>>1);
		VectorClear (p->normal);
		p->normal[i>>1] = -1;
	}
	for ( i = 0 ; i < 6 ; i++ ) {		// the sides' signbits, now that the planes have their normals
		s = &cmg.brushsides[cmg.numBrushSides+i];
		s->planeNum |= CM_PlaneSignbits( &cmg.planes[s->planeNum & CM_SIDE_PLANE] ) << CM_SIDE_SIGNS;
	}
}

/*
===================
CM_TempBoxModel

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
Capsules are handled differently though.
===================
*/
clipHandle_t CM_TempBoxModel( const vec3_t mins, const vec3_t maxs, int capsule ) {

	VectorCopy( mins, box_model.mins );
	VectorCopy( maxs, box_model.maxs );

	if ( capsule ) {
		return CAPSULE_MODEL_HANDLE;
	}

	box_planes[0].dist = maxs[0];
	box_planes[1].dist = -maxs[0];
	box_planes[2].dist = mins[0];
	box_planes[3].dist = -mins[0];
	box_planes[4].dist = maxs[1];
	box_planes[5].dist = -maxs[1];
	box_planes[6].dist = mins[1];
	box_planes[7].dist = -mins[1];
	box_planes[8].dist = maxs[2];
	box_planes[9].dist = -maxs[2];
	box_planes[10].dist = mins[2];
	box_planes[11].dist = -mins[2];

	VectorCopy( mins, box_brush->bounds[0] );
	VectorCopy( maxs, box_brush->bounds[1] );

	return BOX_MODEL_HANDLE;
}

/*
===================
CM_ModelBounds
===================
*/
void CM_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs ) {
	cmodel_t	*cmod;

	cmod = CM_ClipHandleToModel( model );
	VectorCopy( cmod->mins, mins );
	VectorCopy( cmod->maxs, maxs );
}

int CM_LoadSubBSP(const char *name, qboolean clientload)
{
	int		i;
	int		checksum;
	int		count;

	count = cmg.numSubModels;
	for(i = 0; i < NumSubBSP; i++)
	{
		if (!Q_stricmp(name, SubBSP[i].name))
		{
			return count;
		}
		count += SubBSP[i].numSubModels;
	}

	if (NumSubBSP == MAX_SUB_BSP)
	{
		Com_Error (ERR_DROP, "CM_LoadSubBSP: too many unique sub BSPs");
	}

	CM_LoadMap_Actual(name, clientload, &checksum, SubBSP[NumSubBSP] );
	NumSubBSP++;

	return count;
}

// the loader's thread-locals for one parse, put back on every exit, an exception's included
struct cmParseScope {
	cmParseScope( byte *buf, cmChunk_t *c ) {
		cmod_base = buf;
		cm_loadingChunk = qtrue;
		cm_loadingTarget = c;
	}
	~cmParseScope() {
		cm_loadingTarget = NULL;
		cm_loadingChunk = qfalse;
		cmod_base = NULL;
	}
};

/*
==================
CM_ParseChunk

A validated chunk's lumps into malloc'd blocks of its own (CM_Alloc), on either thread. A block that cannot be
allocated, or anything thrown in a loader, fails the parse: its blocks are freed and NULL returned with err
==================
*/
cmChunk_t *CM_ParseChunk( byte *buf, int len, const char *name, const vec3_t origin, char *err, int errLen )
{
	try {
		// the validator allocates (its tree walk's vectors): a bad_alloc on the worker must fail the job, not the
		// process
		if ( !CM_ValidateChunk( buf, len, err, errLen ) ) {
			return NULL;
		}
	} catch ( ... ) {
		snprintf( err, errLen, "out of memory validating" );
		return NULL;
	}
	cmChunk_t *c = (cmChunk_t *)calloc( 1, sizeof( *c ) );
	if ( !c ) {
		snprintf( err, errLen, "out of memory" );
		return NULL;
	}
	clipMap_t &cm = c->cm;
	dheader_t header = *(dheader_t *)buf;
	for ( size_t i = 0 ; i < sizeof( dheader_t ) / 4 ; i++ ) {
		( (int *)&header )[i] = LittleLong( ( (int *)&header )[i] );
	}
	try {
		cmParseScope scope( buf, c );
		CMod_LoadShaders( &header.lumps[LUMP_SHADERS], cm );
		CMod_LoadLeafs( &header.lumps[LUMP_LEAFS], cm );
		CMod_LoadLeafBrushes( &header.lumps[LUMP_LEAFBRUSHES], cm );
		CMod_LoadLeafSurfaces( &header.lumps[LUMP_LEAFSURFACES], cm );
		CMod_LoadPlanes( &header.lumps[LUMP_PLANES], cm );
		CMod_LoadBrushes( &header.lumps[LUMP_BRUSHES], &header.lumps[LUMP_BRUSHSIDES], cm );
		CMod_LoadSubmodels( &header.lumps[LUMP_MODELS], cm );
		CMod_LoadNodes( &header.lumps[LUMP_NODES], cm );
		CMod_LoadVisibility( &header.lumps[LUMP_VISIBILITY], cm );
		CM_FloodAreaConnections( cm );
	} catch ( const cmParseFail &fail ) {
		snprintf( err, errLen, "%s", fail.why );
		CM_FreeChunk( c );
		return NULL;
	} catch ( ... ) {
		snprintf( err, errLen, "a loader failed" );
		CM_FreeChunk( c );
		return NULL;
	}
	Q_strncpyz( cm.name, name, sizeof( cm.name ) );
	VectorCopy( origin, c->origin );
	VectorAdd( cm.cmodels[0].mins, origin, c->mins );
	VectorAdd( cm.cmodels[0].maxs, origin, c->maxs );
	return c;
}


int CM_FindSubBSP(int modelIndex)
{
	int		i;
	int		count;

	count = cmg.numSubModels;
	if (modelIndex < count)
	{	// belongs to the main bsp
		return -1;
	}

	for(i = 0; i < NumSubBSP; i++)
	{
		count += SubBSP[i].numSubModels;
		if (modelIndex < count)
		{
			return i;
		}
	}
	return -1;
}

void CM_GetWorldBounds ( vec3_t mins, vec3_t maxs )
{
	VectorCopy ( cmg.cmodels[0].mins, mins );
	VectorCopy ( cmg.cmodels[0].maxs, maxs );
}

int CM_ModelContents_Actual( clipHandle_t model, clipMap_t *cm )
{
	cmodel_t	*cmod;
	int			contents = 0;
	int			i;

	if (!cm)
	{
		cm = &cmg;
	}

	cmod = CM_ClipHandleToModel( model, &cm );

	//MCG ADDED - return the contents, too

	for ( i = 0; i < cmod->leaf.numLeafBrushes; i++ )
	{
		int brushNum = cm->leafbrushes[cmod->leaf.firstLeafBrush + i];
		contents |= cm->brushes[brushNum].contents;
	}

	for ( i = 0; i < cmod->leaf.numLeafSurfaces; i++ )
	{
		int surfaceNum = cm->leafsurfaces[cmod->leaf.firstLeafSurface + i];
		if ( cm->surfaces[surfaceNum] != NULL )
		{//HERNH?  How could we have a null surf within our cmod->leaf.numLeafSurfaces?
			contents |= cm->surfaces[surfaceNum]->contents;
		}
	}

	return contents;
}

int CM_ModelContents(  clipHandle_t model, int subBSPIndex )
{
	if (subBSPIndex < 0)
	{
		return CM_ModelContents_Actual(model, NULL);
	}

	return CM_ModelContents_Actual(model, &SubBSP[subBSPIndex]);
}
