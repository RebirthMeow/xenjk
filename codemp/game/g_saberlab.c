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

// g_saberlab.c -- scripted saber fights for measurement.
//
// Two bots become fighters with no AI. The lab sends their usercmds the way a
// real client's packets arrive: a command every `cmd` ms, delivered in packets
// every `pkt` ms with no latency, processed before the game frame (level.time
// is still the previous frame's), and never re-run by G_RunClient. So the
// command stream is the same at every sv_fps; only the server's own frame
// changes. Everything else is production code: Pmove, animation, Ghoul2
// collision, the saber pass, G_Damage.
//
// A run is `reps` repetitions of one scripted exchange. Each rep resets both
// fighters (position, view, health, stance, saber state), idles `settle` ms,
// then acts for `act` ms. The action start is shifted by a jitter that sweeps
// 0..jitter-1 ms (rep*13 mod jitter) across reps, so swings start at every
// phase of the server frame and of the 33 ms saber grid. Outcomes are read
// from state, not from instrumented saber code: damage from health deltas,
// clash results from saberMove transitions, locks from saberLockTime.
//
// One CSV row per rep in <homepath>/base/saberlab/<name>.csv. Each rep reseeds
// the game's random streams from seed + rep, so with the same build, map and
// arguments a run replays byte for byte, accelerated or in real time.
// Usage: tools/saber_lab.py; protocol in docs/gameplay/SABER_LAB.md.

#include "g_local.h"
#include "ai_main.h"
#include "g_saberlab.h"

extern void ClientThink_real( gentity_t *ent );
extern qboolean G_SetSaber( gentity_t *ent, int saberNum, char *saberName, qboolean siegeOverride );
extern qboolean G_SaberModelSetup( gentity_t *ent );

enum { LAB_HOLSTER, LAB_IDLE, LAB_SWING, LAB_MASH, LAB_THROW };
enum { LAB_OFF, LAB_SETTLE, LAB_ACT };

#define LAB_SEQ 384
#define LAB_RETURN_MS 15000		// a disarmed saber must be back within this to start a rep

typedef struct {
	int		client;
	int		style, off, def;
	int		action;
	int		fm, rm, up;			// movement keys held with the action
	int		start, hold;		// action window, ms after the act start
	int		hz;					// mash presses a second
	float	yawOffset;			// view yaw relative to facing the other fighter
} labFighter_t;

static struct {
	int				phase;
	qboolean		bound[MAX_CLIENTS];
	char			name[64];
	labFighter_t	f[2];
	vec3_t			anchor;
	float			yaw, dist;
	int				settleMs, actMs, cmdMs, pktMs, hp, jitterRange, reps, traceRep, seed;
	int				latMs, latJitMs;		// packet latency and its uniform jitter
	int				pendProc[2];			// when the next packet is processed (-1: not drawn yet)
	int				lastProc[2];
	unsigned int	netRand;
	int				rep, phaseStart, jitter, settleWait;
	int				lastCmd[2], lastPkt[2];
	fileHandle_t	out;
	// this rep
	int				health[2], dmg[2], hits[2], firstHit[2], disarm[2];
	int				lockMs, passes, frames, inLock;
	char			seq[2][LAB_SEQ];
	const char		*lastCat[2];
	int				lastMove[2];
} lab;

static void LabTrace( const char *what, int i );

static void LabBotThink( int clientNum, int time ) {
	char buf[1024];

	while ( trap->BotGetServerCommand( clientNum, buf, sizeof( buf ) ) ) {
	}
}

qboolean SaberLab_RunClient( gentity_t *ent ) {
	int n = ent - g_entities;

	return n >= 0 && n < MAX_CLIENTS && lab.bound[n];
}

static const char *LabCategory( int m ) {
	if ( m == LS_READY ) return "r";
	if ( m == LS_DRAW ) return "dr";
	if ( m == LS_PUTAWAY ) return "pa";
	if ( m >= LS_A_TL2BR && m < LS_S_TL2BR ) return "A";
	if ( m >= LS_S_TL2BR && m < LS_R_TL2BR ) return "S";
	if ( m >= LS_R_TL2BR && m < LS_T1_BR__R ) return "R";
	if ( m >= LS_T1_BR__R && m < LS_B1_BR ) return "T";
	if ( m >= LS_B1_BR && m < LS_D1_BR ) return "B";		// bounce
	if ( m >= LS_D1_BR && m < LS_V1_BR ) return "D";		// deflected
	if ( m >= LS_V1_BR && m < LS_H1_T_ ) return "V";		// reflected
	if ( m >= LS_H1_T_ && m < LS_K1_T_ ) return "H";		// broken parry
	if ( m >= LS_K1_T_ && m < LS_PARRY_UP ) return "K";		// knockaway
	if ( m >= LS_PARRY_UP && m < LS_REFLECT_UP ) return "P";	// parry
	if ( m >= LS_REFLECT_UP && m < LS_MOVE_MAX ) return "F";	// projectile reflect
	return "x";
}

static void LabSeqAdd( int i, const char *token ) {
	if ( strlen( lab.seq[i] ) + strlen( token ) + 2 >= LAB_SEQ )
		return;
	if ( lab.seq[i][0] )
		Q_strcat( lab.seq[i], LAB_SEQ, ">" );
	Q_strcat( lab.seq[i], LAB_SEQ, token );
}

// Record a change of saberMove category (attacks keep their move number).
static void LabObserveMove( int i ) {
	gclient_t *cl = g_entities[lab.f[i].client].client;
	int m = cl->ps.saberMove;
	const char *cat = LabCategory( m );

	if ( lab.phase != LAB_ACT )
		return;
	if ( cat == lab.lastCat[i] && ( cat[0] != 'A' || m == lab.lastMove[i] ) )
		return;
	lab.lastCat[i] = cat;
	lab.lastMove[i] = m;
	LabSeqAdd( i, cat[0] == 'A' ? va( "A%d", m ) : cat );
}

static void LabEquip( gentity_t *ent, int style ) {
	char single[] = "single_1", staff[] = "dual_1", none[] = "none";
	gclient_t *cl = ent->client;

	if ( style == SS_DUAL ) {
		G_SetSaber( ent, 0, single, qtrue );
		G_SetSaber( ent, 1, single, qtrue );
	} else if ( style == SS_STAFF ) {
		G_SetSaber( ent, 0, staff, qtrue );
		G_SetSaber( ent, 1, none, qtrue );
	} else {
		G_SetSaber( ent, 0, single, qtrue );
		G_SetSaber( ent, 1, none, qtrue );
	}
	G_SaberModelSetup( ent );
	cl->ps.fd.saberAnimLevel = cl->ps.fd.saberAnimLevelBase = cl->ps.fd.saberDrawAnimLevel = style;
	cl->saberCycleQueue = style;
	cl->sess.saberLevel = style;
}

static void LabPlace( int i, vec3_t origin, vec3_t angles ) {
	vec3_t dir;
	float yaw = lab.yaw + ( i ? 180.0f : 0.0f );

	VectorCopy( lab.anchor, origin );
	if ( i ) {
		VectorSet( dir, cosf( DEG2RAD( lab.yaw ) ), sinf( DEG2RAD( lab.yaw ) ), 0.0f );
		VectorMA( lab.anchor, lab.dist, dir, origin );
	}
	VectorSet( angles, 0.0f, AngleNormalize360( yaw + lab.f[i].yawOffset ), 0.0f );
}

static void LabResetFighter( int i ) {
	labFighter_t *f = &lab.f[i];
	gentity_t *ent = &g_entities[f->client];
	gclient_t *cl = ent->client;
	vec3_t origin, angles;

	LabPlace( i, origin, angles );
	VectorCopy( origin, cl->ps.origin );
	VectorCopy( origin, ent->r.currentOrigin );
	VectorClear( cl->ps.velocity );
	SetClientViewAngle( ent, angles );
	cl->ps.pm_type = PM_NORMAL;
	cl->ps.pm_flags &= ~( PMF_TIME_KNOCKBACK | PMF_TIME_WATERJUMP );
	cl->ps.pm_time = 0;

	ent->health = cl->ps.stats[STAT_HEALTH] = cl->ps.stats[STAT_MAX_HEALTH] = lab.hp;
	cl->ps.stats[STAT_ARMOR] = 0;
	cl->ps.fd.forcePower = cl->ps.fd.forcePowerMax = 100;
	cl->ps.fd.forcePowersActive = 0;
	cl->ps.fd.forcePowersKnown |= ( 1 << FP_SABER_OFFENSE ) | ( 1 << FP_SABER_DEFENSE );
	cl->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] = f->off;
	cl->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = f->def;
	if ( f->action == LAB_THROW ) {   // saber throw at rank 3 (the throw itself is the stock or spear throw)
		cl->ps.fd.forcePowersKnown |= ( 1 << FP_SABERTHROW );
		cl->ps.fd.forcePowerLevel[FP_SABERTHROW] = FORCE_LEVEL_3;
	}
	cl->ps.fd.saberAnimLevel = cl->ps.fd.saberAnimLevelBase = cl->ps.fd.saberDrawAnimLevel = f->style;

	cl->ps.weapon = WP_SABER;
	cl->ps.weaponstate = WEAPON_READY;
	cl->ps.weaponTime = 0;
	cl->ps.saberHolstered = f->action == LAB_HOLSTER ? 2 : 0;
	cl->ps.saberMove = LS_READY;
	cl->ps.saberBlocked = BLOCKED_NONE;
	cl->ps.saberAttackChainCount = 0;
	cl->ps.saberLockTime = 0;
	cl->ps.saberLockFrame = 0;
	cl->ps.saberLockHits = 0;
	cl->ps.saberLockAdvance = qfalse;
	cl->ps.saberLockEnemy = ENTITYNUM_NONE;
	cl->ps.torsoTimer = cl->ps.legsTimer = 0;
	cl->ps.forceHandExtend = HANDEXTEND_NONE;
	cl->ps.forceHandExtendTime = 0;
	cl->ps.forceDodgeAnim = 0;
	trap->LinkEntity( (sharedEntity_t *)ent );
}

static void LabStartRep( void ) {
	int i;

	for ( i = 0; i < 2; i++ ) {
		LabResetFighter( i );
		lab.health[i] = lab.hp;
		lab.dmg[i] = lab.hits[i] = lab.disarm[i] = 0;
		lab.firstHit[i] = -1;
		lab.seq[i][0] = 0;
		lab.lastCat[i] = NULL;
		lab.lastMove[i] = -1;
	}
	lab.lockMs = lab.passes = lab.frames = lab.inLock = 0;
	lab.jitter = lab.jitterRange > 1 ? ( lab.rep * 13 ) % lab.jitterRange : 0;
	// Each rep draws from its own stream: what ran before it (map load, bot spawns, how the
	// engine paced frames) cannot change it, and any rep can be replayed alone.
	lab.netRand = (unsigned int)( lab.seed + lab.rep * 7919 ) ^ 0x5bd1e995u;
	Rand_Init( lab.seed + lab.rep * 7919 );
	srand( lab.seed + lab.rep * 7919 );
	lab.phase = LAB_SETTLE;
	lab.phaseStart = level.time;
	lab.settleWait = 0;
}

static void LabWriteRow( void ) {
	char *row = va( "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s\n",
		lab.rep, lab.jitter, level.time - level.previousTime, g_tickSabers.integer,
		lab.dmg[0], lab.dmg[1], lab.hits[0], lab.hits[1], lab.firstHit[0], lab.firstHit[1],
		lab.lockMs, lab.passes, lab.frames, lab.disarm[0], lab.disarm[1],
		lab.seq[0][0] ? lab.seq[0] : "-", lab.seq[1][0] ? lab.seq[1] : "-" );

	if ( lab.out )
		trap->FS_Write( row, strlen( row ), lab.out );
}

static void LabFinish( const char *why ) {
	int i;

	if ( lab.out ) {
		trap->FS_Close( lab.out );
		lab.out = 0;
	}
	// Hand the fighters back: G_RunClient runs them again and the bot code
	// may think for them.
	for ( i = 0; i < 2; i++ ) {
		int n = lab.f[i].client;

		if ( n < 0 || n >= MAX_CLIENTS )
			continue;
		if ( lab.bound[n] && botstates[n] && botstates[n]->customThink == LabBotThink )
			botstates[n]->customThink = NULL;
	}
	memset( lab.bound, 0, sizeof( lab.bound ) );
	lab.phase = LAB_OFF;
	G_LogPrintf( "SABERLAB_DONE name=%s reps=%d result=%s\n", lab.name, lab.rep, why );
	trap->Print( "SABERLAB_DONE name=%s reps=%d result=%s\n", lab.name, lab.rep, why );
}

// The fighter's command at serverTime t.
static void LabCommand( int i, int t, usercmd_t *cmd ) {
	labFighter_t *f = &lab.f[i];
	gclient_t *cl = g_entities[f->client].client;
	vec3_t origin, angles;
	int rel = t - ( lab.phaseStart + lab.jitter ), j;

	memset( cmd, 0, sizeof( *cmd ) );
	cmd->serverTime = t;
	cmd->weapon = WP_SABER;
	LabPlace( i, origin, angles );
	for ( j = 0; j < 3; j++ )
		cmd->angles[j] = ANGLE2SHORT( angles[j] ) - cl->ps.delta_angles[j];
	if ( lab.phase != LAB_ACT || rel < f->start || rel >= f->start + f->hold )
		return;
	switch ( f->action ) {
	case LAB_SWING:
		cmd->buttons = BUTTON_ATTACK;
		break;
	case LAB_MASH:
		if ( f->hz > 0 && ( ( rel - f->start ) * f->hz * 2 / 1000 ) % 2 == 0 )
			cmd->buttons = BUTTON_ATTACK;
		break;
	case LAB_THROW:
		cmd->buttons = BUTTON_ALT_ATTACK;
		break;
	default:
		return;
	}
	cmd->forwardmove = f->fm;
	cmd->rightmove = f->rm;
	cmd->upmove = f->up;
}

static void LabDeliver( int i, int arrival ) {
	gentity_t *ent = &g_entities[lab.f[i].client];
	usercmd_t cmd;
	int t;

	for ( t = lab.lastCmd[i] + lab.cmdMs; t <= arrival; t += lab.cmdMs ) {
		LabCommand( i, t, &cmd );
		ent->client->pers.cmd = cmd;
		G_TickLatchForceButtons( ent->client, &ent->client->pers.cmd );
		ent->client->lastCmdTime = level.time;
		ClientThink_real( ent );
		lab.lastCmd[i] = t;
		LabTrace( "cmd", i );
		LabObserveMove( i );
	}
}

// trace=<rep>: every command and frame of one rep, to find where two runs part.
static void LabTrace( const char *what, int i ) {
	gclient_t *cl;

	if ( lab.rep != lab.traceRep || lab.phase == LAB_OFF )
		return;
	cl = g_entities[lab.f[i].client].client;
	G_LogPrintf( "SABERLAB_TRACE %s rep=%d phase=%d lt=%d ct=%d f=%d o=%.3f,%.3f,%.3f v=%.3f,%.3f,%.3f yaw=%.3f mv=%d blk=%d ta=%d tt=%d la=%d lt2=%d hp=%d wt=%d rng=%d pass=%d tip=%.2f,%.2f,%.2f base=%.2f,%.2f,%.2f st=%d nom=%d\n",
		what, lab.rep, lab.phase, level.time, cl->ps.commandTime, i, cl->ps.origin[0], cl->ps.origin[1], cl->ps.origin[2],
		cl->ps.velocity[0], cl->ps.velocity[1], cl->ps.velocity[2], cl->ps.viewangles[YAW], cl->ps.saberMove, cl->ps.saberBlocked,
		cl->ps.torsoAnim, cl->ps.torsoTimer, cl->ps.legsAnim, cl->ps.legsTimer, g_entities[lab.f[i].client].health,
		cl->ps.weaponTime, Rand_State(), level.saberPassFrame,
		cl->saber[0].blade[0].muzzlePoint[0] + cl->saber[0].blade[0].muzzleDir[0] * cl->saber[0].blade[0].lengthMax,
		cl->saber[0].blade[0].muzzlePoint[1] + cl->saber[0].blade[0].muzzleDir[1] * cl->saber[0].blade[0].lengthMax,
		cl->saber[0].blade[0].muzzlePoint[2] + cl->saber[0].blade[0].muzzleDir[2] * cl->saber[0].blade[0].lengthMax,
		cl->saber[0].blade[0].muzzlePoint[0], cl->saber[0].blade[0].muzzlePoint[1], cl->saber[0].blade[0].muzzlePoint[2],
		cl->saber[0].blade[0].storageTime, level.saberPassNominal );
}

// What the last game frame did to the fighters.
static void LabObserveFrame( void ) {
	int i, lock = 0;

	LabTrace( "frame", 0 );
	LabTrace( "frame", 1 );
	if ( lab.phase != LAB_ACT )
		return;
	lab.frames++;
	if ( level.saberPassFrame )
		lab.passes++;
	for ( i = 0; i < 2; i++ ) {
		gentity_t *ent = &g_entities[lab.f[i].client];
		int h = ent->health, other = 1 - i;

		if ( h < lab.health[i] ) {
			lab.dmg[other] += lab.health[i] - h;
			lab.hits[other]++;
			if ( lab.firstHit[other] < 0 )
				lab.firstHit[other] = level.time - ( lab.phaseStart + lab.jitter );
		}
		lab.health[i] = h;
		if ( ent->client->ps.saberInFlight )
			lab.disarm[i] = 1;
		if ( ent->client->ps.saberLockTime > level.time )
			lock = 1;
	}
	if ( lock ) {
		lab.lockMs += level.time - level.previousTime;
		if ( !lab.inLock ) {
			LabSeqAdd( 0, "L" );
			LabSeqAdd( 1, "L" );
		}
	}
	lab.inLock = lock;
}

void SaberLab_PreFrame( int levelTime ) {
	int i;

	if ( lab.phase == LAB_OFF )
		return;
	for ( i = 0; i < 2; i++ ) {
		gentity_t *ent = &g_entities[lab.f[i].client];
		if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED || ent->health <= 0 ) {
			LabFinish( "fighter_lost" );
			return;
		}
	}
	LabObserveFrame();

	if ( lab.phase == LAB_SETTLE && level.time >= lab.phaseStart + lab.settleMs ) {
		if ( g_entities[lab.f[0].client].client->ps.saberInFlight || g_entities[lab.f[1].client].client->ps.saberInFlight ) {
			// a disarmed saber is still on its way back
			if ( ( lab.settleWait += level.time - level.previousTime ) > LAB_RETURN_MS ) {
				LabFinish( "saber_not_returned" );
				return;
			}
		} else {
			for ( i = 0; i < 2; i++ ) {
				lab.health[i] = g_entities[lab.f[i].client].health;
				LabObserveMove( i );
			}
			lab.phase = LAB_ACT;
			lab.phaseStart = level.time;
			for ( i = 0; i < 2; i++ ) {
				lab.lastCat[i] = NULL;
				LabObserveMove( i );
			}
		}
	} else if ( lab.phase == LAB_ACT && level.time >= lab.phaseStart + lab.jitter + lab.actMs ) {
		LabWriteRow();
		if ( ++lab.rep >= lab.reps ) {
			LabFinish( "ok" );
			return;
		}
		LabStartRep();
	}

	// Packets processed by this frame, both fighters in processing order. A packet is sent
	// every pkt ms carrying the commands since the last; it is processed lat + U(0, latjit)
	// ms later, never before the previous one (no reordering). The draw uses the lab's own
	// stream, so the game's random numbers are untouched.
	for ( ;; ) {
		int k;

		for ( i = 0; i < 2; i++ ) {
			if ( lab.pendProc[i] < 0 ) {
				int sent = lab.lastPkt[i] + lab.pktMs, delay = lab.latMs;

				if ( lab.latJitMs > 0 ) {
					lab.netRand = lab.netRand * 1103515245u + 12345u;
					delay += ( lab.netRand >> 16 ) % ( lab.latJitMs + 1 );
				}
				lab.pendProc[i] = Q_max( lab.lastProc[i], sent + delay );
			}
		}
		k = lab.pendProc[1] < lab.pendProc[0] ? 1 : 0;
		if ( lab.pendProc[k] > levelTime )
			break;
		LabDeliver( k, lab.lastPkt[k] + lab.pktMs );
		lab.lastPkt[k] += lab.pktMs;
		lab.lastProc[k] = lab.pendProc[k];
		lab.pendProc[k] = -1;
	}
}

static int LabStyle( const char *s ) {
	if ( !Q_stricmp( s, "fast" ) ) return SS_FAST;
	if ( !Q_stricmp( s, "medium" ) ) return SS_MEDIUM;
	if ( !Q_stricmp( s, "strong" ) ) return SS_STRONG;
	if ( !Q_stricmp( s, "desann" ) ) return SS_DESANN;
	if ( !Q_stricmp( s, "tavion" ) ) return SS_TAVION;
	if ( !Q_stricmp( s, "dual" ) ) return SS_DUAL;
	if ( !Q_stricmp( s, "staff" ) ) return SS_STAFF;
	return atoi( s );
}

static int LabAction( const char *s ) {
	if ( !Q_stricmp( s, "holster" ) ) return LAB_HOLSTER;
	if ( !Q_stricmp( s, "swing" ) ) return LAB_SWING;
	if ( !Q_stricmp( s, "mash" ) ) return LAB_MASH;
	if ( !Q_stricmp( s, "throw" ) ) return LAB_THROW;
	return LAB_IDLE;
}

// key=value; fighter keys take an a. or b. prefix.
static qboolean LabSet( const char *key, const char *value ) {
	labFighter_t *f = NULL;

	if ( ( key[0] == 'a' || key[0] == 'b' ) && key[1] == '.' ) {
		f = &lab.f[key[0] == 'b'];
		key += 2;
		if ( !Q_stricmp( key, "client" ) ) f->client = atoi( value );
		else if ( !Q_stricmp( key, "style" ) ) f->style = LabStyle( value );
		else if ( !Q_stricmp( key, "off" ) ) f->off = atoi( value );
		else if ( !Q_stricmp( key, "def" ) ) f->def = atoi( value );
		else if ( !Q_stricmp( key, "act" ) ) f->action = LabAction( value );
		else if ( !Q_stricmp( key, "fm" ) ) f->fm = atoi( value );
		else if ( !Q_stricmp( key, "rm" ) ) f->rm = atoi( value );
		else if ( !Q_stricmp( key, "up" ) ) f->up = atoi( value );
		else if ( !Q_stricmp( key, "start" ) ) f->start = atoi( value );
		else if ( !Q_stricmp( key, "hold" ) ) f->hold = atoi( value );
		else if ( !Q_stricmp( key, "hz" ) ) f->hz = atoi( value );
		else if ( !Q_stricmp( key, "yaw" ) ) f->yawOffset = atof( value );
		else return qfalse;
		return qtrue;
	}
	if ( !Q_stricmp( key, "name" ) ) Q_strncpyz( lab.name, value, sizeof( lab.name ) );
	else if ( !Q_stricmp( key, "reps" ) ) lab.reps = atoi( value );
	else if ( !Q_stricmp( key, "dist" ) ) lab.dist = atof( value );
	else if ( !Q_stricmp( key, "settle" ) ) lab.settleMs = atoi( value );
	else if ( !Q_stricmp( key, "act" ) ) lab.actMs = atoi( value );
	else if ( !Q_stricmp( key, "cmd" ) ) lab.cmdMs = atoi( value );
	else if ( !Q_stricmp( key, "pkt" ) ) lab.pktMs = atoi( value );
	else if ( !Q_stricmp( key, "hp" ) ) lab.hp = atoi( value );
	else if ( !Q_stricmp( key, "jitter" ) ) lab.jitterRange = atoi( value );
	else if ( !Q_stricmp( key, "yaw" ) ) lab.yaw = atof( value );
	else if ( !Q_stricmp( key, "trace" ) ) lab.traceRep = atoi( value );
	else if ( !Q_stricmp( key, "seed" ) ) lab.seed = atoi( value );
	else if ( !Q_stricmp( key, "lat" ) ) lab.latMs = atoi( value );
	else if ( !Q_stricmp( key, "latjit" ) ) lab.latJitMs = atoi( value );
	else if ( !Q_stricmp( key, "anchor" ) ) {
		if ( sscanf( value, "%f,%f,%f", &lab.anchor[0], &lab.anchor[1], &lab.anchor[2] ) != 3 )
			return qfalse;
	}
	else return qfalse;
	return qtrue;
}

// The first deathmatch spawn in entity order, and the first of 16 directions with a clear
// lane; neither depends on the random seed, so every tickrate fights on the same floor.
static qboolean LabAutoAnchor( gentity_t *probe ) {
	gentity_t *spot = G_Find( NULL, FOFS( classname ), "info_player_deathmatch" );
	int d;

	if ( !spot )
		return qfalse;
	VectorCopy( spot->s.origin, lab.anchor );
	lab.anchor[2] += 9;
	for ( d = 0; d < 16; d++ ) {
		trace_t tr;
		vec3_t dir, end;
		float yaw = d * 22.5f;

		VectorSet( dir, cosf( DEG2RAD( yaw ) ), sinf( DEG2RAD( yaw ) ), 0.0f );
		VectorMA( lab.anchor, lab.dist + 64.0f, dir, end );
		trap->Trace( &tr, lab.anchor, probe->r.mins, probe->r.maxs, end, probe->s.number, MASK_SOLID, qfalse, 0, 0 );
		if ( !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f ) {
			lab.yaw = yaw;
			return qtrue;
		}
	}
	return qfalse;
}

static const char *LabTimescale( void ) {
	static char ts[32];

	trap->Cvar_VariableStringBuffer( "timescale", ts, sizeof( ts ) );
	return ts;
}

static void LabStart( void ) {
	char arg[MAX_TOKEN_CHARS], *eq;
	char path[MAX_QPATH];
	int i, n = trap->Argc();
	qboolean autoAnchor = qtrue;

	if ( lab.phase != LAB_OFF ) {
		trap->Print( "saberlab: a run is active; saberlab stop first\n" );
		return;
	}
	memset( lab.f, 0, sizeof( lab.f ) );
	lab.f[0].client = 0; lab.f[1].client = 1;
	for ( i = 0; i < 2; i++ ) {
		lab.f[i].style = SS_MEDIUM;
		lab.f[i].off = lab.f[i].def = FORCE_LEVEL_3;
		lab.f[i].action = LAB_IDLE;
		lab.f[i].hold = 0;
	}
	Q_strncpyz( lab.name, "run", sizeof( lab.name ) );
	lab.reps = 100; lab.dist = 64; lab.settleMs = 800; lab.actMs = 1500;
	lab.cmdMs = 8; lab.pktMs = 8; lab.hp = 999; lab.jitterRange = 33; lab.traceRep = -1; lab.seed = 1; lab.latMs = 0; lab.latJitMs = 0;
	for ( i = 2; i < n; i++ ) {
		trap->Argv( i, arg, sizeof( arg ) );
		eq = strchr( arg, '=' );
		if ( !eq ) {
			trap->Print( "saberlab: expected key=value, got \"%s\"\n", arg );
			return;
		}
		*eq = 0;
		if ( !Q_stricmp( arg, "anchor" ) )
			autoAnchor = qfalse;
		if ( !LabSet( arg, eq + 1 ) ) {
			trap->Print( "saberlab: unknown or bad key \"%s\"\n", arg );
			return;
		}
	}
	if ( lab.reps < 1 || lab.cmdMs < 1 || lab.pktMs < 1 || lab.actMs < 1 || lab.hp < 1 || lab.jitterRange < 1 ) {
		trap->Print( "saberlab: reps, cmd, pkt, act, hp and jitter must be positive\n" );
		return;
	}
	for ( i = 0; i < 2; i++ ) {
		gentity_t *ent = &g_entities[lab.f[i].client];
		if ( lab.f[i].client < 0 || lab.f[i].client >= MAX_CLIENTS || !ent->inuse || !ent->client ||
			ent->client->pers.connected != CON_CONNECTED || !( ent->r.svFlags & SVF_BOT ) || !botstates[lab.f[i].client] ) {
			trap->Print( "saberlab: client %d is not a connected bot\n", lab.f[i].client );
			return;
		}
	}
	if ( lab.f[0].client == lab.f[1].client ) {
		trap->Print( "saberlab: a and b must be different clients\n" );
		return;
	}
	if ( autoAnchor && !LabAutoAnchor( &g_entities[lab.f[0].client] ) ) {
		trap->Print( "saberlab: no deathmatch spawn with a clear lane; pass anchor=x,y,z yaw=deg\n" );
		return;
	}
	Com_sprintf( path, sizeof( path ), "saberlab/%s.csv", lab.name );
	trap->FS_Open( path, &lab.out, FS_WRITE );
	if ( !lab.out ) {
		trap->Print( "saberlab: cannot write %s\n", path );
		return;
	}
	{
		char *head = va( "# saberlab name=%s reps=%d dist=%.1f anchor=%.1f,%.1f,%.1f yaw=%.1f settle=%d act=%d cmd=%d pkt=%d lat=%d latjit=%d hp=%d jitter=%d "
			"sv_fps=%d timescale=%s g_tickSabers=%d seed=%d "
			"a=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d b=%d/%d/%d/%d/%d/%d/%d/%d/%d/%d/%d\n"
			"rep,jitter,frame_ms,tick_sabers,a_dmg,b_dmg,a_hits,b_hits,a_first,b_first,lock_ms,passes,frames,a_disarm,b_disarm,a_seq,b_seq\n",
			lab.name, lab.reps, lab.dist, lab.anchor[0], lab.anchor[1], lab.anchor[2], lab.yaw, lab.settleMs, lab.actMs,
			lab.cmdMs, lab.pktMs, lab.latMs, lab.latJitMs, lab.hp, lab.jitterRange, trap->Cvar_VariableIntegerValue( "sv_fps" ),
			LabTimescale(), g_tickSabers.integer, lab.seed,
			lab.f[0].style, lab.f[0].off, lab.f[0].def, lab.f[0].action, lab.f[0].fm, lab.f[0].rm, lab.f[0].up, lab.f[0].start, lab.f[0].hold, lab.f[0].hz, (int)lab.f[0].yawOffset,
			lab.f[1].style, lab.f[1].off, lab.f[1].def, lab.f[1].action, lab.f[1].fm, lab.f[1].rm, lab.f[1].up, lab.f[1].start, lab.f[1].hold, lab.f[1].hz, (int)lab.f[1].yawOffset );
		trap->FS_Write( head, strlen( head ), lab.out );
	}
	for ( i = 0; i < 2; i++ ) {
		gentity_t *ent = &g_entities[lab.f[i].client];
		botstates[lab.f[i].client]->customThink = LabBotThink;
		lab.bound[lab.f[i].client] = qtrue;
		ent->flags |= FL_NOTARGET;
		LabEquip( ent, lab.f[i].style );
		lab.lastCmd[i] = lab.lastPkt[i] = lab.lastProc[i] = level.time;
		lab.pendProc[i] = -1;
	}
	lab.rep = 0;
	LabStartRep();
	trap->Print( "SABERLAB_START name=%s reps=%d anchor=%.1f,%.1f,%.1f yaw=%.1f\n", lab.name, lab.reps,
		lab.anchor[0], lab.anchor[1], lab.anchor[2], lab.yaw );
}

void SaberLab_Shutdown( void ) {
	if ( lab.phase != LAB_OFF )
		LabFinish( "map_shutdown" );
}

void Svcmd_SaberLab_f( void ) {
	char sub[MAX_TOKEN_CHARS];

	trap->Argv( 1, sub, sizeof( sub ) );
	if ( !Q_stricmp( sub, "start" ) ) {
		LabStart();
	} else if ( !Q_stricmp( sub, "status" ) ) {
		trap->Print( "SABERLAB_STATUS name=%s active=%d rep=%d reps=%d time=%d\n", lab.name, lab.phase != LAB_OFF,
			lab.rep, lab.reps, level.time );
	} else if ( !Q_stricmp( sub, "stop" ) ) {
		if ( lab.phase != LAB_OFF )
			LabFinish( "stopped" );
	} else {
		trap->Print( "Usage: saberlab start [key=value ...] | status | stop  (keys: docs/gameplay/SABER_LAB.md)\n" );
	}
}
