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
 * pmove_style_test.c -- a player's real pmove (bg_pmove.c Pmove, bg_slidemove.c, bg_saber.c) on an analytic floor and
 * ramp, driven by one scripted tape, for the movement styles (bg_public.h movementStyle_e; CPM from
 * TaystJK behind a cvar, and a freestyle mode).
 *
 *   pmove_style_test STYLE [-d]
 *     STYLE  0 stock, 1 CPM, 2 freestyle (ps->stats[STAT_MOVEMENTSTYLE]; ps->speed 250, or 320 for 1 and 2 as
 *            g_active.c sets it)
 *     -d     dump every command's state as hex floats (byte comparison of two builds: stock must not change)
 *
 * The tape, 8 ms commands (125 fps):
 *   run       2 s forward from a standstill
 *   strafe    8 s strafe-jumping: forward + right, jump at every landing (stock must let go between jumps; CPM
 *             holds), the view kept at the angle that gains most speed for the style's air acceleration
 *   aircontrol  forward only, in the air after a jump, turning the view at 180 deg/s: how far the velocity turns
 *   sidestrafe  right only, in the air after a jump, turning at 180 deg/s
 *   ramp      run at a 20 deg ramp and jump on it
 * Each phase prints its speeds and turns.
 *
 * Build like tools/bike/bike_pmove_test.c (the game module's _GAME bg sources and these stubs):
 *   gcc -O2 -w -D_GAME -I codemp -I codemp/game -I shared -I <build>/shared tools/movement/pmove_style_test.c
 *       codemp/game/bg_*.c codemp/game/be_pmove.c codemp/qcommon/q_shared.c shared/qcommon/q_{math,string,color}.c
 *       -lm -o pmove_style_test.exe
 */

#include "g_local.h"

#define XCVAR_DECL
	#include "g_xcvar.h"
#undef XCVAR_DECL

/* ---- what bg_* calls in the game module, stubbed ---- */

gameImport_t		*trap;
level_locals_t		level;
gentity_t			g_entities[MAX_GENTITIES];
vmCvar_t			bg_fighterAltControl;

static int			stubTime;

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
}
int G_EffectIndex( const char *name ) { (void)name; return 0; }
void G_BikeFrameContact( bgEntity_t *bike, const trace_t *trace, const vec3_t point, float speedIn, float speedAlong )
{
	(void)bike; (void)trace; (void)point; (void)speedIn; (void)speedAlong;
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

/* ---- the world: solid below the floor z = 0, and below a 20 deg ramp rising along +x from x = RAMP_X along the
   y < RAMP_Y half (the ground is the higher of the two there) ---- */

#define SURF_EPS	0.125f	/* SURFACE_CLIP_EPSILON */
#define RAMP_X		1.0e6f	/* far away until the ramp phase moves it */

typedef struct { vec3_t n; float d; } worldPlane_t;	/* solid where n.p < d */

static worldPlane_t	planes[2];
static int			numPlanes;

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

static void SetRamp( float x0, float deg )
{	/* plane 1: rising along +x from x0 (deg 0: a copy of the floor) */
	float r = DEG2RAD( deg );

	VectorSet( planes[1].n, -sinf( r ), 0, cosf( r ) );
	planes[1].d = -sinf( r ) * x0;
	numPlanes = 2;
}

/* ---- the player ---- */

static pmove_t			pmv;
static gclient_t		client;
static animation_t		anims[MAX_TOTALANIMATIONS];
static int				dump, style, t;
static float			rampX;

static void Setup( void )
{
	gentity_t		*e = &g_entities[0];
	playerState_t	*ps = &client.ps;
	int				i;

	memset( g_entities, 0, sizeof( g_entities ) );
	memset( &client, 0, sizeof( client ) );
	memset( &pmv, 0, sizeof( pmv ) );
	for ( i = 0; i < MAX_TOTALANIMATIONS; i++ )
	{
		anims[i].firstFrame = 0;
		anims[i].numFrames = 10;
		anims[i].frameLerp = 50;
		anims[i].loopFrames = -1;
	}
	VectorSet( planes[0].n, 0, 0, 1 );
	planes[0].d = 0;
	SetRamp( RAMP_X, 0 );

	e->s.number = 0;
	e->s.eType = ET_PLAYER;
	e->client = &client;
	e->playerState = ps;
	e->inuse = qtrue;
	ps->clientNum = 0;
	ps->pm_type = PM_NORMAL;
	ps->gravity = 800;
	ps->speed = ( style == MV_CPM || style == MV_FREESTYLE ) ? 250.0f * 1.28f : 250.0f;	/* g_active.c */
	ps->basespeed = ps->speed;
	ps->weapon = WP_MELEE;
	ps->stats[STAT_HEALTH] = ps->stats[STAT_MAX_HEALTH] = 100;
	ps->stats[STAT_WEAPONS] = 1 << WP_MELEE;
	ps->stats[STAT_MOVEMENTSTYLE] = style;
	ps->fd.forcePowersKnown = 1 << FP_LEVITATION;
	ps->fd.forcePowerLevel[FP_LEVITATION] = FORCE_LEVEL_3;
	ps->fd.forcePower = ps->fd.forcePowerMax = 100;
	ps->groundEntityNum = ENTITYNUM_WORLD;
	ps->origin[2] = 24.0f + SURF_EPS;	/* standing: mins z -24 */
	ps->commandTime = 0;

	pmv.ps = ps;
	pmv.trace = PlaneTrace;
	pmv.pointcontents = PointContents;
	pmv.tracemask = MASK_PLAYERSOLID;
	VectorSet( pmv.mins, -15, -15, DEFAULT_MINS_2 );
	VectorSet( pmv.maxs, 15, 15, DEFAULT_MAXS_2 );
	VectorSet( pmv.modelScale, 1, 1, 1 );
	pmv.baseEnt = (bgEntity_t *)g_entities;
	pmv.entSize = sizeof( gentity_t );
	pmv.animations = anims;
	pmv.gametype = GT_FFA;
	pmv.noFootsteps = qtrue;
	pmv.stepSlideFix = 1;
	pm = &pmv;
}

static usercmd_t	cmd;

static void Command( void )
{
	playerState_t *ps = &client.ps;

	t += 8;
	cmd.serverTime = t;
	/* view angles go through delta_angles as the engine sends them */
	pmv.cmd = cmd;
	stubTime = level.time = t;
	Pmove( &pmv );
	if ( dump )
	{
		union { float f; unsigned u; } c[10];
		int i;

		c[0].f = ps->origin[0]; c[1].f = ps->origin[1]; c[2].f = ps->origin[2];
		c[3].f = ps->velocity[0]; c[4].f = ps->velocity[1]; c[5].f = ps->velocity[2];
		c[6].f = ps->viewangles[YAW]; c[7].f = ps->viewangles[PITCH]; c[8].f = ps->speed;
		c[9].f = (float)ps->fd.forcePower;
		printf( "t=%d", t );
		for ( i = 0; i < 10; i++ )
			printf( " %08x", c[i].u );
		printf( " g=%d f=%x l=%d e=%d\n", ps->groundEntityNum, ps->pm_flags, ps->legsAnim, ps->eventSequence );
	}
}

static void SetYaw( float yaw )
{
	cmd.angles[YAW] = ANGLE2SHORT( yaw ) - client.ps.delta_angles[YAW];
	cmd.angles[PITCH] = 0;
}

static float HSpeed( void ) { return sqrtf( client.ps.velocity[0] * client.ps.velocity[0] + client.ps.velocity[1] * client.ps.velocity[1] ); }
static float VelYaw( void ) { return RAD2DEG( atan2f( client.ps.velocity[1], client.ps.velocity[0] ) ); }
static qboolean OnGround( void ) { return client.ps.groundEntityNum != ENTITYNUM_NONE; }

int main( int argc, char **argv )
{
	playerState_t	*ps = &client.ps;
	static gameImport_t imports;
	float			peak = 0, v, turned, yaw, gain;
	int				i, jumps = 0, held = 0;

	if ( argc < 2 )
	{
		fprintf( stderr, "usage: pmove_style_test STYLE [-d]\n" );
		return 2;
	}
	style = atoi( argv[1] );
	dump = ( argc > 2 && !strcmp( argv[2], "-d" ) );
	imports.SnapVector = Stub_SnapVector;
	trap = &imports;
	Setup();

	/* run: 2 s forward along +x */
	memset( &cmd, 0, sizeof( cmd ) );
	SetYaw( 0 );
	cmd.forwardmove = 127;
	for ( i = 0; i < 250; i++ )
		Command();
	printf( "style %d run: %.1f u/s after 2 s\n", style, HSpeed() );

	/* strafe: 8 s of strafe-jumping, forward + right, the view at the best angle for the air acceleration */
	cmd.rightmove = 127;
	for ( i = 0; i < 1000; i++ )
	{
		v = HSpeed();
		if ( OnGround() )
		{	/* jump at once, let go in the air (a tap: freestyle's held jump would be a force jump) */
			cmd.upmove = 127;
			jumps += !held;
			held = 1;
		}
		else
		{
			cmd.upmove = 0;
			held = 0;
		}
		/* wishdir is the view yaw - 45 (forward + right); keep it at acos((wish - a wish dt) / v) from the velocity:
		   the angle where the air acceleration (1 u/s per u/s of wishspeed a second) adds the most speed */
		gain = 0;
		if ( v > 0 && ( ps->speed - 1.0f * ps->speed * 0.008f ) / v < 1.0f )
			gain = RAD2DEG( acosf( ( ps->speed - 1.0f * ps->speed * 0.008f ) / v ) );
		yaw = VelYaw() - gain + 45.0f;
		SetYaw( yaw );
		Command();
		if ( HSpeed() > peak )
			peak = HSpeed();
	}
	printf( "style %d strafe: %d jumps in 8 s, peak %.1f u/s, end %.1f u/s\n", style, jumps, peak, HSpeed() );

	/* coast to a stop, then jump with forward only and turn the view 180 deg/s while airborne */
	memset( &cmd, 0, sizeof( cmd ) );
	SetYaw( VelYaw() );
	for ( i = 0; i < 250; i++ )
		Command();
	cmd.forwardmove = 127;
	SetYaw( 0 );
	for ( i = 0; i < 125; i++ )
		Command();
	yaw = 0;
	v = VelYaw();
	cmd.upmove = 127;
	Command();
	cmd.upmove = 0;
	i = 0;
	while ( !OnGround() && i++ < 200 )
	{
		yaw += 180.0f * 0.008f;
		SetYaw( yaw );
		Command();
	}
	turned = AngleSubtract( VelYaw(), v );
	printf( "style %d aircontrol: forward only, view turned %.0f deg in %d ms of air: velocity turned %.1f deg, %.1f u/s\n",
		style, yaw, i * 8, turned, HSpeed() );

	/* side strafe: right only in the air, turning the view 180 deg/s */
	memset( &cmd, 0, sizeof( cmd ) );
	for ( i = 0; i < 250; i++ )
		Command();
	SetYaw( 0 );
	cmd.forwardmove = 127;
	for ( i = 0; i < 125; i++ )
		Command();
	cmd.forwardmove = 0;
	cmd.rightmove = 127;
	v = VelYaw();
	yaw = 0;
	cmd.upmove = 127;
	Command();
	cmd.upmove = 0;
	i = 0;
	while ( i++ < 200 && !( i > 5 && OnGround() ) )
	{
		yaw -= 180.0f * 0.008f;
		SetYaw( yaw );
		Command();
	}
	turned = AngleSubtract( VelYaw(), v );
	printf( "style %d sidestrafe: right only, view turned %.0f deg in %d ms of air: velocity turned %.1f deg, %.1f u/s\n",
		style, yaw, i * 8, turned, HSpeed() );

	/* ramp: run up a 20 deg ramp 200 u ahead and jump on it */
	memset( &cmd, 0, sizeof( cmd ) );
	for ( i = 0; i < 250; i++ )
		Command();
	rampX = ps->origin[0] + 200.0f;
	SetRamp( rampX, 20.0f );
	SetYaw( 0 );
	cmd.forwardmove = 127;
	while ( ps->origin[0] < rampX + 60.0f && t < 200000 )
		Command();
	cmd.upmove = 127;
	Command();
	v = ps->velocity[2];
	cmd.upmove = 0;
	for ( i = 0; i < 150; i++ )
		Command();
	printf( "style %d ramp: 20 deg ramp jump at %.0f u/s up (%.0f u up the ramp, legs anim %d)\n", style, v,
		ps->origin[0] - rampX, ps->legsAnim );

	/* hold jump: standing on the floor, jump held 1.5 s (stock and freestyle: a force jump; CPM: hops) */
	SetRamp( RAMP_X, 0 );
	memset( &cmd, 0, sizeof( cmd ) );
	for ( i = 0; i < 250; i++ )
		Command();
	v = ps->origin[2];
	cmd.upmove = 127;
	for ( i = 0, peak = v, jumps = 0; i < 190; i++ )
	{
		qboolean was = OnGround();

		Command();
		jumps += was && !OnGround();
		if ( ps->origin[2] > peak )
			peak = ps->origin[2];
	}
	printf( "style %d holdjump: 1.5 s held: top %.1f u up, %d takeoffs\n", style, peak - v, jumps );
	printf( "style %d force %d of %d\n", style, ps->fd.forcePower, ps->fd.forcePowerMax );
	return 0;
}
