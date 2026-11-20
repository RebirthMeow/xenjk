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
 * bike_pmove_test.c -- drives the real bike pmove (bg_pmove.c Pmove -> PmoveSingle -> bg_bikemove.c
 * PM_BikeVehicleMove -> bg_bike.c) over an analytic inclined plane at several command rates, and fails when the
 * result depends on the rate. bike_test_main.c covers the integrator alone (heightfield translate); this covers
 * the pmove adapter, where the per-command ground trace and velocity snap live.
 *
 * Built with the game module's _GAME bg sources and the stubs below (tools/bike/tape_harness.py --pmove-exe runs a
 * build of it; nothing builds it for you):
 *   gcc -O2 -w -D_GAME -I codemp -I codemp/game -I shared -I <build>/shared tools/bike/bike_pmove_test.c
 *       codemp/game/bg_*.c codemp/game/be_pmove.c codemp/qcommon/q_shared.c shared/qcommon/q_{math,string,color}.c
 *       -lm -o bike_pmove_test.exe
 *   bike_pmove_test.exe [-v | -t] [-b FILE.bike]   (-b: a handling file; default the generic bike)
 *
 * Output: one line per case and split with the largest divergence from the 4 ms run at the 528 ms checkpoints,
 * then "PASS" or "FAIL"; exit status 1 on any failure (divergence over tolerance, a NaN, a missing checkpoint).
 */

#include "g_local.h"
#include "bg_bike.h"

#define XCVAR_DECL
	#include "g_xcvar.h"
#undef XCVAR_DECL

/* ---- what bg_* calls in the game module, stubbed ---- */

gameImport_t		*trap;
level_locals_t		level;
gentity_t			g_entities[MAX_GENTITIES];
vmCvar_t			bg_fighterAltControl;

static int			stubTime;
static int			damageCalls;
static int			staleSpeeds;	/* commands whose ps->speed is not the forward speed of the velocity left */
static int			traceCalls;
static int			traceRun;		/* -t: print the 4 ms reference runs */
static int			frameContacts;	/* G_BikeFrameContact calls (sparks / crash checks) */
static float		frameContactMax;	/* the hardest, u/s into the plane */

int BG_GetTime( void ) { return stubTime; }
void Bot2_ForecastPmoveMark( int phase ) { (void)phase; }
void Client_CheckImpactBBrush( gentity_t *self, gentity_t *other ) { (void)self; (void)other; }
qboolean FighterIsLanded( Vehicle_t *pVeh, playerState_t *parentPS ) { (void)pVeh; (void)parentPS; return qfalse; }
void G_AddEvent( gentity_t *ent, int event, int eventParm ) { (void)ent; (void)event; (void)eventParm; }
qboolean G_CanBeEnemy( gentity_t *self, gentity_t *enemy ) { (void)self; (void)enemy; return qfalse; }
void G_CheapWeaponFire( int entNum, int ev ) { (void)entNum; (void)ev; }
void G_Damage( gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point, int damage,
	int dflags, int mod )
{
	(void)targ; (void)inflictor; (void)attacker; (void)dir; (void)point; (void)damage; (void)dflags; (void)mod;
	damageCalls++;
}
int G_EffectIndex( const char *name ) { (void)name; return 0; }
void G_BikeFrameContact( bgEntity_t *bike, const trace_t *trace, const vec3_t point, float speedIn, float speedAlong )
{
	(void)bike; (void)trace; (void)point; (void)speedAlong;
	frameContacts++;
	if ( speedIn > frameContactMax )
		frameContactMax = speedIn;
}
int G_ModelIndex( const char *name ) { (void)name; return 0; }
int G_SoundIndex( const char *name ) { (void)name; return 0; }
void G_FlyVehicleSurfaceDestruction( gentity_t *veh, trace_t *trace, int magnitude, qboolean force )
{
	(void)veh; (void)trace; (void)magnitude; (void)force;
}
gentity_t *G_PlayEffect( int fxID, vec3_t org, vec3_t ang ) { (void)fxID; (void)org; (void)ang; return NULL; }
gentity_t *G_PlayEffectID( const int fxID, vec3_t org, vec3_t ang ) { (void)fxID; (void)org; (void)ang; return NULL; }
void NPC_SetAnim( gentity_t *ent, int type, int anim, int priority ) { (void)ent; (void)type; (void)anim; (void)priority; }
qboolean TryGrapple( gentity_t *ent ) { (void)ent; return qfalse; }
void WP_GetVehicleCamPos( gentity_t *ent, gentity_t *pilot, vec3_t camPos ) { (void)ent; (void)pilot; (void)camPos; }
void G_SetAnimalVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }
void G_SetFighterVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }
void G_SetSharedVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }
void G_SetSpeederVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }
void G_SetBikeVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }
void G_SetWalkerVehicleFunctions( vehicleInfo_t *pVehInfo ) { (void)pVehInfo; }

static void QDECL Stub_Printf( const char *msg, ... ) { (void)msg; }
static void NORETURN QDECL Stub_Error( int level, const char *error, ... )
{
	va_list ap;

	(void)level;
	va_start( ap, error );
	vfprintf( stderr, error, ap );
	va_end( ap );
	fputc( '\n', stderr );
	exit( 2 );
}
NORETURN_PTR void ( *Com_Error )( int level, const char *error, ... ) = Stub_Error;
void ( *Com_Printf )( const char *msg, ... ) = Stub_Printf;

static void Stub_SnapVector( float *v )
{	/* Sys_SnapVector: round to nearest */
	v[0] = (float)lrintf( v[0] );
	v[1] = (float)lrintf( v[1] );
	v[2] = (float)lrintf( v[2] );
}

static qboolean Veh_Update( Vehicle_t *pVeh, const usercmd_t *pUcmd ) { (void)pVeh; (void)pUcmd; return qtrue; }
static void Veh_Animate( Vehicle_t *pVeh ) { (void)pVeh; }
static qboolean Veh_UpdateRider( Vehicle_t *pVeh, bgEntity_t *pRider, usercmd_t *pUcmd )
{
	(void)pVeh; (void)pRider; (void)pUcmd;
	return qtrue;
}
static void Veh_AttachRiders( Vehicle_t *pVeh ) { (void)pVeh; }

/* ---- the world: solid below an inclined plane through the origin (rising along +x), optionally a wall and a ramp
   (a second walkable plane rising from rampX: the ground is the higher of the two) ---- */

#define SURF_EPS	0.125f	/* SURFACE_CLIP_EPSILON */

typedef struct { vec3_t n; float d; } worldPlane_t;	/* solid where n.p < d */

static worldPlane_t	planes[3];
static float		caseRampX, caseRampDeg;	/* the ramp of the case being run (rampX 0: none) */
static int			numPlanes;

/* signed clearance of the box's deepest corner from plane k at p */
static float BoxClearance( int k, const vec3_t p, const vec3_t mins, const vec3_t maxs )
{
	const float	*n = planes[k].n;
	float		s = DotProduct( n, p ) - planes[k].d;
	int			i;

	for ( i = 0; i < 3; i++ )
		s += n[i] * ( n[i] > 0 ? mins[i] : maxs[i] );
	return s;
}

static void PlaneTrace( trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
	int passEntityNum, int contentMask )
{	/* the collision model's semantics for a union of half-spaces: hit SURF_EPS short, startsolid / allsolid */
	int		k, hit = -1;
	float	best = 1.0f;

	(void)passEntityNum; (void)contentMask;
	traceCalls++;
	memset( tr, 0, sizeof( *tr ) );
	tr->entityNum = ENTITYNUM_NONE;
	for ( k = 0; k < numPlanes; k++ )
	{
		float d1 = BoxClearance( k, start, mins, maxs ), d2 = BoxClearance( k, end, mins, maxs ), f;

		if ( d1 <= 0 )
		{
			tr->startsolid = qtrue;
			if ( d2 <= 0 )
			{
				tr->allsolid = qtrue;
				best = 0;
				hit = k;
			}
			continue;
		}
		if ( d2 >= SURF_EPS || d2 >= d1 )
			continue;
		f = ( d1 - SURF_EPS ) / ( d1 - d2 );
		if ( f < 0 )
			f = 0;
		if ( f < best || hit < 0 )
		{
			best = f;
			hit = k;
		}
	}
	tr->fraction = ( hit >= 0 ) ? best : 1.0f;
	tr->endpos[0] = start[0] + tr->fraction * ( end[0] - start[0] );
	tr->endpos[1] = start[1] + tr->fraction * ( end[1] - start[1] );
	tr->endpos[2] = start[2] + tr->fraction * ( end[2] - start[2] );
	if ( hit >= 0 )
	{
		VectorCopy( planes[hit].n, tr->plane.normal );
		tr->plane.dist = planes[hit].d;
		tr->entityNum = ENTITYNUM_WORLD;
		tr->contents = CONTENTS_SOLID;
	}
}

static int PointContents( const vec3_t point, int passEntityNum ) { (void)point; (void)passEntityNum; return 0; }

/* ---- the run ---- */

#define PILOT_NUM	0
#define VEH_NUM		(MAX_CLIENTS + 4)
#define SEG_MS		528

typedef struct { float throttle, steer; int walk, crouch, jump; } tapeSeg_t;

static const tapeSeg_t tape[] = {
	{ 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 },
	{ 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0 },	/* full throttle (up the slope) */
	{ 1, 0.5f, 0, 0, 0 }, { 1, 0.5f, 0, 0, 0 },	/* a turn */
	{ 1, 0, 1, 0, 0 },					/* tucked */
	{ 0, 0, 0, 0, 0 },					/* coast */
	{ -1, 0, 0, 0, 0 }, { -1, 0, 0, 0, 0 },	/* brake */
};
#define NUM_SEGS	( (int)( sizeof( tape ) / sizeof( tape[0] ) ) )
#define TAPE_END	( SEG_MS * NUM_SEGS )

typedef struct { float x, y, z, yaw, speed, pitch, lean; } checkpoint_t;

static gclient_t	vehClient, pilotClient;
static Vehicle_t	veh;
static vehicleInfo_t vehInfo;
static bikeParams_t	fileParams;	/* -b FILE: a handling file (ext_data/vehicles/<name>.bike) */
static int			useFileParams;
static pmove_t		pmv;

static void SetupWorld( float slopeDeg, float startYaw, float wallX, int pmoveFloat );

/* one command (veh.m_ucmd) ending at serverTime, the way the server runs the vehicle's pmove */
static void Command( int serverTime )
{
	veh.m_ucmd.serverTime = serverTime;
	pmv.cmd = veh.m_ucmd;
	stubTime = level.time = serverTime;
	Pmove( &pmv );
}

/* split > 0: commands of that many ms; 0: a mixed sequence of 1..41 ms commands */
/* wallX > 0: a wall facing -x at that x */
static int RunCase( float slopeDeg, float startYaw, float wallX, int split, int pmoveFloat, checkpoint_t *cps )
{
	playerState_t *vps = &vehClient.ps;
	int			t = 0, n = 0, len, next, seg;
	unsigned int seed = 12345;

	SetupWorld( slopeDeg, startYaw, wallX, pmoveFloat );
	while ( t < TAPE_END )
	{
		next = ( t / SEG_MS + 1 ) * SEG_MS;
		if ( split > 0 )
			len = split;
		else
		{
			seed = seed * 1103515245u + 12345u;
			len = 1 + (int)( ( seed >> 16 ) & 0x7fff ) % 41;	/* tape_harness.py's mixed split */
		}
		if ( t + len > next )
			len = next - t;
		seg = t / SEG_MS;
		memset( &veh.m_ucmd, 0, sizeof( veh.m_ucmd ) );
		veh.m_ucmd.forwardmove = (signed char)( tape[seg].throttle * 127 );
		veh.m_ucmd.rightmove = (signed char)( tape[seg].steer * 127 );
		veh.m_ucmd.upmove = (signed char)( tape[seg].jump ? 127 : tape[seg].crouch ? -127 : 0 );
		veh.m_ucmd.buttons = tape[seg].walk ? BUTTON_WALKING : 0;
		Command( t + len );
		t += len;
		if ( traceRun && split == 4 && t % 132 == 0 )
			printf( "  t=%5d pos=(%.1f %.1f %.2f) vz=%.1f pitch=%.2f speed=%.1f ground=%d mins_z=%.2f\n", t, vps->origin[0],
				vps->origin[1], vps->origin[2], vps->velocity[2], -vps->vehOrientation[PITCH], vps->speed, vps->groundEntityNum,
				pmv.mins[2] );
		if ( vps->commandTime != t )
			return -1;
		{	/* the dismount's roll-off test reads ps->speed: it must be the speed after the slide move */
			float yr = DEG2RAD( vps->vehOrientation[YAW] );

			if ( fabsf( vps->speed - ( vps->velocity[0] * cosf( yr ) + vps->velocity[1] * sinf( yr ) ) ) > 1.0f )
				staleSpeeds++;
		}
		if ( t % SEG_MS == 0 )
		{
			cps[n].x = vps->origin[0];
			cps[n].y = vps->origin[1];
			cps[n].z = vps->origin[2];
			cps[n].yaw = vps->vehOrientation[YAW];
			cps[n].speed = vps->speed;
			cps[n].pitch = vps->vehOrientation[PITCH];
			cps[n].lean = vps->vehOrientation[ROLL];
			n++;
		}
	}
	return n;
}

/* ridden 1 s at full throttle and turning, then left with the handbrake on (100 ms thinks, as NPC.c's for an empty
   vehicle) for 6 s, then the ground drops 40 u. Returns the failures: it must come to rest, sleep (a few traces a
   think, not the substep loop's ~50) and fall when the ground goes. Prints the state it rests in at full precision
   for an A/B against a build without the fast path. */
static int RunParked( void )
{
	playerState_t *vps = &vehClient.ps;
	int		t = 0, k, restTraces = 0, fails = 0;
	float	z;

	SetupWorld( 0, 0, 0, 0 );
	for ( t = 8; t <= 1056; t += 8 )
	{
		memset( &veh.m_ucmd, 0, sizeof( veh.m_ucmd ) );
		veh.m_ucmd.forwardmove = 127;
		veh.m_ucmd.rightmove = 60;
		Command( t );
	}
	t = 1056;
	vps->m_iVehicleNum = 0;		/* the rider is off */
	veh.m_pPilot = NULL;
	memset( &veh.m_ucmd, 0, sizeof( veh.m_ucmd ) );
	for ( k = 0; k < 60; k++ )
	{
		traceCalls = 0;
		t += 100;
		Command( t );
		if ( k >= 50 )
			restTraces += traceCalls;
	}
	printf( "parked after 6 s: (%.9g %.9g %.9g) v (%g %g %g) yaw %.9g pitch %.9g lean %.9g; %.1f traces a 100 ms think "
		"at rest%s\n", vps->origin[0], vps->origin[1], vps->origin[2], vps->velocity[0], vps->velocity[1],
		vps->velocity[2], vps->vehOrientation[YAW], vps->vehOrientation[PITCH], vps->vehOrientation[ROLL],
		restTraces / 10.0f, veh.m_bikeAsleep ? ", asleep" : "" );
	if ( VectorLength( vps->velocity ) > 0.0f || restTraces > 10 * 6 )	/* the substep loop: about 50 */
	{
		printf( "parked bike did not come to rest or kept simulating FAIL\n" );
		fails++;
	}
	z = vps->origin[2];
	planes[0].d = -40;
	for ( k = 0; k < 10; k++ )
	{
		t += 100;
		Command( t );
	}
	printf( "ground dropped 40 u: the bike fell %.1f u%s\n", z - vps->origin[2], z - vps->origin[2] > 10 ? "" : " FAIL" );
	fails += !( z - vps->origin[2] > 10 );
	return fails;
}

static void SetupWorld( float slopeDeg, float startYaw, float wallX, int pmoveFloat )
{
	static gameImport_t	imports;
	gentity_t	*v = &g_entities[VEH_NUM], *pilot = &g_entities[PILOT_NUM];
	playerState_t *vps = &vehClient.ps;
	float		th = DEG2RAD( slopeDeg );

	memset( g_entities, 0, sizeof( g_entities ) );
	memset( &vehClient, 0, sizeof( vehClient ) );
	memset( &pilotClient, 0, sizeof( pilotClient ) );
	memset( &veh, 0, sizeof( veh ) );
	memset( &vehInfo, 0, sizeof( vehInfo ) );
	memset( &imports, 0, sizeof( imports ) );
	imports.SnapVector = Stub_SnapVector;
	trap = &imports;
	VectorSet( planes[0].n, -sinf( th ), 0, cosf( th ) );
	planes[0].d = 0;
	VectorSet( planes[1].n, -1, 0, 0 );
	planes[1].d = -wallX;
	numPlanes = ( wallX > 0 ) ? 2 : 1;
	if ( caseRampX > 0 )
	{
		float r = DEG2RAD( caseRampDeg );

		if ( numPlanes == 1 )
			planes[1] = planes[0];	/* no wall: the slot holds a copy of the ground (no effect on the union) */
		VectorSet( planes[2].n, -sinf( r ), 0, cosf( r ) );
		planes[2].d = -sinf( r ) * caseRampX;
		numPlanes = 3;
	}

	vehInfo.type = VH_BIKE;
	vehInfo.bike = useFileParams ? &fileParams : NULL;	/* NULL: the generic bike */
	vehInfo.length = 93;	/* ct3x.veh; BG_VehicleAdjustBBoxForOrientation sets the box only with all three */
	vehInfo.width = 48;
	vehInfo.height = 64;
	vehInfo.mass = 500;		/* ct3x.veh: the speeder impact path's magnitude scales with it */
	vehInfo.toughness = 80;
	vehInfo.Update = Veh_Update;
	vehInfo.Animate = Veh_Animate;
	vehInfo.UpdateRider = Veh_UpdateRider;
	vehInfo.AttachRiders = Veh_AttachRiders;
	veh.m_pVehicleInfo = &vehInfo;
	veh.m_pParentEntity = (bgEntity_t *)v;
	veh.m_pPilot = (bgEntity_t *)pilot;

	v->s.number = VEH_NUM;
	v->s.NPC_class = CLASS_VEHICLE;
	v->s.eType = ET_NPC;	/* as spawned: PM_SlideMove's vehicle impact path keys on it */
	v->client = &vehClient;
	v->playerState = vps;
	v->m_pVehicle = &veh;
	v->inuse = qtrue;
	pilot->s.number = PILOT_NUM;
	pilot->client = &pilotClient;
	pilot->playerState = &pilotClient.ps;
	pilot->inuse = qtrue;
	pilotClient.ps.clientNum = PILOT_NUM;
	pilotClient.ps.m_iVehicleNum = VEH_NUM;

	vps->clientNum = VEH_NUM;
	vps->m_iVehicleNum = PILOT_NUM + 1;
	vps->pm_type = PM_NORMAL;
	vps->groundEntityNum = ENTITYNUM_WORLD;
	vps->vehOrientation[YAW] = vps->viewangles[YAW] = startYaw;
	vps->gravity = 800;
	vps->speed = 0;

	memset( &pmv, 0, sizeof( pmv ) );
	pmv.ps = vps;
	pmv.trace = PlaneTrace;
	pmv.pointcontents = PointContents;
	pmv.tracemask = MASK_PLAYERSOLID;
	VectorSet( pmv.mins, -vehInfo.width / 2, -vehInfo.width / 2, DEFAULT_MINS_2 );
	VectorSet( pmv.maxs, vehInfo.width / 2, vehInfo.width / 2, vehInfo.height + DEFAULT_MINS_2 );
	VectorSet( pmv.modelScale, 1, 1, 1 );
	pmv.baseEnt = (bgEntity_t *)g_entities;
	pmv.entSize = sizeof( gentity_t );
	pmv.pmove_float = pmoveFloat;
	pmv.stepSlideFix = 1;
	pmv.noFootsteps = qtrue;
	pm = &pmv;	/* PM_BikeVehicleMove reads the global pm */

	/* rest on the plane: the lowest corner SURF_EPS above it */
	VectorClear( vps->origin );
	vps->origin[2] = ( SURF_EPS - BoxClearance( 0, vps->origin, pmv.mins, pmv.maxs ) ) / planes[0].n[2];

}

static float BikeMaxf( float a, float b ) { return ( b > a || b != b ) ? b : a; }	/* a NaN wins */

static int Finite( const checkpoint_t *c )
{
	return isfinite( c->x ) && isfinite( c->y ) && isfinite( c->z ) && isfinite( c->yaw ) && isfinite( c->speed )
		&& isfinite( c->pitch ) && isfinite( c->lean );
}

int main( int argc, char **argv )
{
	/* minRise: a climb. At the end of the straight full throttle (3168 ms) the bike must be at least that high (u) and
	   still going up. On a 16.5 deg road the frame's flat
	   box rested on any slope over 4.2 deg with the rear tyre in the air, and every climb here slid back down while
	   the split gates passed */
	static const struct { const char *name; float slope, yaw, wallX, rampX, rampDeg; int contacts; float minRise; }
		cases[] = {
		{ "flat", 0, 0, 0, 0, 0, 0, 0 },
		{ "up5", 5, 0, 0, 0, 0, 0, 100 },
		{ "up15", 15, 0, 0, 0, 0, 0, 200 },
		{ "up16", 16.5f, 0, 0, 0, 0, 0, 200 },		/* the reported road's grade */
		{ "up16d", 16.5f, 45, 0, 0, 0, 0, 150 },	/* up it at 45 deg: the box's corner leads */
		{ "up25", 25, 0, 0, 0, 0, 0, 150 },
		{ "across", 16.5f, 90, 0, 0, 0, 0, 0 },	/* along the hillside: the cross slope under the box */
		{ "down10", 10, 180, 0, 0, 0, 0, 0 },
		{ "down25", 25, 180, 0, 0, 0, 0, 0 },
		{ "ramp", 0, 0, 0, 600, 12, 0, 150 },	/* flat into a 12 deg ramp: the slope change is ground, not a hit */
		{ "ramp16", 0, 0, 0, 600, 16.5f, 0, 200 },
		{ "wall", 0, 0, 1500, 0, 0, 1, 0 },	/* into a wall at full throttle, then along it: the frame hits */
	};
	static const int splits[] = { 8, 16, 33, 50, 100, 0 };
	const float posTol = 0.05f, speedTol = 0.05f, angTol = 0.01f;	/* float noise; the defects were metres */
	checkpoint_t ref[NUM_SEGS], cur[NUM_SEGS];
	int c, s, f, i, nRef, nCur, fails = 0;
	int verbose = 0;

	for ( i = 1; i < argc; i++ )
	{
		if ( !strcmp( argv[i], "-v" ) )
			verbose = 1;
		else if ( !strcmp( argv[i], "-t" ) )
			traceRun = 1;
		else if ( !strcmp( argv[i], "-b" ) && i + 1 < argc )
		{	/* the handling file the game would read for the bike */
			static char text[64 * 1024];
			char		bad[64];
			FILE		*fp = fopen( argv[++i], "rb" );
			size_t		len;

			if ( !fp )
			{
				fprintf( stderr, "cannot open %s\n", argv[i] );
				return 2;
			}
			len = fread( text, 1, sizeof( text ) - 1, fp );
			fclose( fp );
			text[len] = 0;
			BG_BikeDefaultParams( &fileParams );
			if ( BG_BikeParseParams( &fileParams, text, bad, sizeof( bad ) ) )
			{
				fprintf( stderr, "%s: lines not understood (first '%s')\n", argv[i], bad );
				return 2;
			}
			useFileParams = 1;
		}
	}

	for ( c = 0; c < (int)ARRAY_LEN( cases ); c++ )
	{
		int damage0 = damageCalls;

		caseRampX = cases[c].rampX;
		caseRampDeg = cases[c].rampDeg;
		frameContacts = 0;
		frameContactMax = 0;
		nRef = RunCase( cases[c].slope, cases[c].yaw, cases[c].wallX, 4, 0, ref );
		if ( nRef != NUM_SEGS )
		{
			printf( "%s: reference run returned %d of %d checkpoints FAIL\n", cases[c].name, nRef, NUM_SEGS );
			fails++;
			continue;
		}
		printf( "%-6s 4 ms reference: after %d ms (%.1f %.1f %.1f) speed %.1f u/s; at 3168 ms %.1f u/s\n",
			cases[c].name, TAPE_END, ref[NUM_SEGS - 1].x, ref[NUM_SEGS - 1].y, ref[NUM_SEGS - 1].z,
			ref[NUM_SEGS - 1].speed, ref[5].speed );
		if ( cases[c].minRise > 0 )
		{
			int bad = !( ref[5].z >= cases[c].minRise && ref[5].speed > 0.0f );

			fails += bad;
			printf( "%-6s climb: at 3168 ms %.0f u up (at least %.0f), %.0f u/s %s\n", cases[c].name, ref[5].z,
				cases[c].minRise, ref[5].speed, bad ? "FAIL" : "ok" );
		}
		for ( f = 0; f < 2; f++ )
		{
			for ( s = 0; s < (int)ARRAY_LEN( splits ) + 1; s++ )
			{
				int split = ( s == 0 ) ? 4 : splits[s - 1];
				float dPos = 0, dSpeed = 0, dAng = 0, d;
				int bad;

				if ( split == 4 && f == 0 )
					continue;	/* the reference itself */
				nCur = RunCase( cases[c].slope, cases[c].yaw, cases[c].wallX, split, f, cur );
				bad = ( nCur != NUM_SEGS );
				for ( i = 0; i < nCur && i < NUM_SEGS; i++ )
				{
					if ( !Finite( &cur[i] ) )
					{
						bad = 1;
						break;
					}
					d = sqrtf( ( cur[i].x - ref[i].x ) * ( cur[i].x - ref[i].x ) + ( cur[i].y - ref[i].y ) * ( cur[i].y - ref[i].y )
						+ ( cur[i].z - ref[i].z ) * ( cur[i].z - ref[i].z ) );
					dPos = BikeMaxf( dPos, d );
					dSpeed = BikeMaxf( dSpeed, fabsf( cur[i].speed - ref[i].speed ) );
					dAng = BikeMaxf( dAng, fabsf( AngleSubtract( cur[i].yaw, ref[i].yaw ) ) );
					dAng = BikeMaxf( dAng, fabsf( cur[i].pitch - ref[i].pitch ) );
					dAng = BikeMaxf( dAng, fabsf( cur[i].lean - ref[i].lean ) );
					if ( verbose )
						printf( "   t=%d speed %.3f vs %.3f\n", ( i + 1 ) * SEG_MS, cur[i].speed, ref[i].speed );
				}
				bad |= !( dPos <= posTol && dSpeed <= speedTol && dAng <= angTol );	/* NaN fails too */
				fails += bad;
				printf( "%-6s %s pmove_float %d vs 4 ms: pos %9.4g u  speed %9.4g u/s  angles %9.4g deg  (%d checkpoints) %s\n",
					cases[c].name, split ? va( "%3d ms", split ) : " mixed", f, dPos, dSpeed, dAng, nCur,
					bad ? "FAIL" : "ok" );
			}
		}
		{	/* frame contact: none on ground of any slope or slope change; the wall is hit (all runs of the case) */
			int bad = cases[c].contacts ? ( frameContacts == 0 ) : ( frameContacts != 0 || damageCalls != damage0 );

			fails += bad;
			printf( "%-6s frame contacts %d (hardest %.0f u/s into the plane), G_Damage calls %d: %s\n", cases[c].name,
				frameContacts, frameContactMax, damageCalls - damage0, bad ? "FAIL" : "ok" );
		}
	}
	caseRampX = 0;
	fails += RunParked();
	if ( staleSpeeds )
	{
		printf( "ps->speed differed from the post-move forward speed after %d commands FAIL\n", staleSpeeds );
		fails++;
	}
	printf( "%s: %d failing comparisons; G_Damage calls %d\n", fails ? "FAIL" : "PASS", fails, damageCalls );
	return fails ? 1 : 0;
}
