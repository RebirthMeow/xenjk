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

#include "qcommon/q_shared.h"
#include "qfiles.h"

void		CM_LoadMap( const char *name, qboolean clientload, int *checksum);

void		CM_ClearMap( void );
int			CM_LoadChunk( const char *name, const vec3_t origin );	// a collision chunk (cm_local.h)
void		CM_LoadChunk_f( void );
void		CM_ChunkManifest_f( void );		// residency (docs/levelport-collision.md section 16)
void		CM_Drive_f( void );
void		CM_DriveMove_f( void );
void		CM_ChunkReport_f( void );
void		CM_ChunkResidency( const vec3_t *origins, const vec3_t *velocities, int numOrigins );	// velocities may be NULL
qboolean	CM_ChunkReadyAt( const vec3_t p );	// qfalse while p is in a chunk not swapped in: hold a client
qboolean	CM_HasChunks( void );	// a manifest is read or a chunk is placed on this map
cvar_t		*CM_ChunkSpawnNearCvar( void );			// cm_chunkSpawnNear (the spawn preload and the server's hold)
cvar_t		*CM_ChunkFirstEntryNearCvar( void );	// sv_chunkFirstEntryNear
void		CM_NoteServerFrame( int ms );
void		CM_PreloadSpawns( void );
void		CM_PreloadStatics( void );	// the chunks under the hub's static items, before map_restart's entities
void		CM_ChunkPreload_f( void );
void		CM_ChunkPending_f( void );
qboolean	CM_ChunkRegionReady( const vec3_t p );	// qfalse while a chunk around p is not in: a spawning client waits
int			CM_ChunksMissingNear( const vec3_t p, float dist );	// chunks within dist of p not in (0: the load distance)
int			CM_ChunkLoadCount( void );					// chunks swapped or loaded in since the manifest was read
void		CM_NoteSpawnHold( int ms, int waited, int loads, int missingAtRelease, int deadAtRelease );
int			CM_ChunksDeadNear( const vec3_t p, float dist );	// given-up chunks within dist of p (0: the load distance)
void		CM_NoteClientHeld( void );
void		CM_ChunkShutdown( void );	// the residency worker stopped and joined (Com_Shutdown)
clipHandle_t CM_InlineModel( int index );		// 0 = world, 1 + are bmodels
clipHandle_t CM_TempBoxModel( const vec3_t mins, const vec3_t maxs, int capsule );

void		CM_ModelBounds( clipHandle_t model, vec3_t mins, vec3_t maxs );

int			CM_NumInlineModels( void );
char		*CM_EntityString (void);

// returns an ORed contents mask
int			CM_PointContents( const vec3_t p, clipHandle_t model );
int			CM_TransformedPointContents( const vec3_t p, clipHandle_t model, const vec3_t origin, const vec3_t angles );

void		CM_BoxTrace ( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, int capsule );
void		CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, clipHandle_t model, int brushmask, const vec3_t origin, const vec3_t angles, int capsule );

byte		*CM_ClusterPVS (int cluster);

int			CM_PointLeafnum( const vec3_t p );

// only returns non-solid leafs
// overflow if return listsize and if *lastLeaf != list[listsize-1]
int			CM_BoxLeafnums( const vec3_t mins, const vec3_t maxs, int *boxList,
		 					int listsize, int *lastLeaf );
//rwwRMG - changed to boxList to not conflict with list type

int			CM_LeafCluster (int leafnum);
int			CM_LeafArea (int leafnum);

void		CM_AdjustAreaPortalState( int area1, int area2, qboolean open );
qboolean	CM_AreasConnected( int area1, int area2 );

int			CM_WriteAreaBits( byte *buffer, int area );

//rwwRMG - added:
bool		CM_GenericBoxCollide(const vec3pair_t abounds, const vec3pair_t bbounds);
void		CM_CalcExtents(const vec3_t start, const vec3_t end, const struct traceWork_s *tw, vec3pair_t bounds);

// cm_tag.c
int			CM_LerpTag( orientation_t *tag,  clipHandle_t model, int startFrame, int endFrame,
					 float frac, const char *tagName );


// cm_marks.c
int	CM_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
				   int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );

// cm_patch.c
void CM_DrawDebugSurface( void (*drawPoly)(int color, int numPoints, float *points) );

// cm_trace.cpp
bool CM_CullWorldBox (const cplane_t *frustum, const vec3pair_t bounds);
