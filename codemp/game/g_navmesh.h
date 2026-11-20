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

#ifndef G_NAVMESH_H
#define G_NAVMESH_H

#ifdef __cplusplus
extern "C" {
#endif

// Use raw floats to avoid struct alignment issues between C and C++
typedef float navVec3_t[3];

// Area types for off-mesh connections — must match generator constants
#define OFFMESH_AREA_NONE         0
#define OFFMESH_AREA_JUMP_DROP    2   // walk/fall off ledge, no jump needed
#define OFFMESH_AREA_JUMP_BASIC   3   // running jump across a gap
#define OFFMESH_AREA_WALLRUN      4   // wallrun up near-vertical surface
#define OFFMESH_AREA_ELEVATOR     10  // func_plat / vertical func_door
#define OFFMESH_AREA_JUMPPAD      11  // trigger_push: connection start at the trigger
                                      // footprint, end at the upper-ledge landing wp

// Extended waypoint result returned by NavMesh_GetNextWaypointEx.
// `position` is the immediate next nav waypoint (same as NavMesh_GetNextWaypoint).
// When an off-mesh connection appears anywhere in the path to the destination,
// `offmeshType` is non-zero and `offmeshStart`/`offmeshEnd` hold the connection
// endpoints in Quake space.  The bot should suppress ledge avoidance while
// offmeshType != 0, and activate the appropriate traversal state when it arrives
// within the connection's activation radius of offmeshStart.
typedef struct {
	float position[3];     // immediate next waypoint (Quake space)
	int   offmeshType;     // OFFMESH_AREA_* — 0 if no off-mesh connection in path
	float offmeshStart[3]; // connection start point  (Quake space)
	float offmeshEnd[3];   // connection end/landing  (Quake space)
} NavMeshWaypoint_t;

void  NavMesh_InitForMap(const char* mapname);
void  NavMesh_Free(void);
int   NavMesh_GetNextWaypoint(int passEntityNum, const float* startPoint, const float* endPoint, float* outWaypoint);
// avoidAreaMask: bitmask of area types to heavily penalise — pathfinder routes around them
// when an alternative exists, but still uses them as last resort (bit N = penalise area N).
// Pass 0 for normal shortest-path behaviour.
int   NavMesh_GetNextWaypointEx(int passEntityNum, const float* startPoint, const float* endPoint, NavMeshWaypoint_t* outInfo, int avoidAreaMask);
int   NavMesh_GetPath(int passEntityNum, const float* startPoint, const float* endPoint, float* outWaypoints, int maxWaypoints);
float NavMesh_GetPathDistance(int passEntityNum, const float* startPoint, const float* endPoint);
int   NavMesh_IsPointOnMesh(const float* point);
void  NavMesh_DrawDebug(const float* center, float radius);
// typeMask: bitmask of area types to draw. Bit N = draw area N. 0 = draw all.
void  NavMesh_DrawOffMeshDebug(const float* center, float radius, int typeMask);
void  NavMesh_PrintDebugInfo(void);
int   NavMesh_GetBounds(float* outMin, float* outMax);
// The index-th off-mesh connection of the loaded mesh, in Quake space: its
// area type and its start and end points. Returns 0 past the last one.
int   NavMesh_OffMeshConnection(int index, int* outType, float* outStart, float* outEnd);
void  NavMesh_Log(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // G_NAVMESH_H
