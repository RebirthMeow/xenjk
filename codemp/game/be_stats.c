/*
===========================================================================
Copyright (C) 2014 - 2026, base_enhanced contributors (TheSil, deathsythe47, Avygeil)
Copyright (C) 2026, xenjk contributors

This file is part of xenjk, an OpenJK derivative, and carries code ported from
base_enhanced (https://github.com/avygeil/base_enhanced, GPLv2).

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

// be_stats.c -- base_enhanced CTF statistics, quarantined.
//
// Ported from base_enhanced 774700c (game/g_stats.c, and the stats lines of
// g_combat.c, g_team.c, g_items.c, g_weapon.c, g_missile.c, w_force.c,
// g_main.c, g_cmds.c, g_svcmds.c); each function names what it mirrors.
// Six tables (general, force, damage, weapon given, weapon taken, accuracy),
// /stats /ctfstats /stat with subcommands and help (also in intermission),
// the end-of-match broadcast, the rcon/console command, CTF region time and
// the automatic base/chase/offense detection over five-minute blocks.
//
// Passive by construction: every hook reads game state and writes only this
// file's memory. Nothing here touches an entity, a playerState, a
// configstring, the RNG or the clock, and no real-time value is read. The
// only engine calls are reads (userinfo-free; a plain BSP trace for air shots)
// and prints. Prints never go to bots: a bot does not drain its reliable
// commands in intermission and an overflow would drop it.
//
// Differences from upstream, all because xenjk lacks the system:
// - Identity: no accounts or session database. A player is keyed by ja_guid
//   (name when a human has none; bots by slot and name), so stats survive a
//   reconnect and stay split per team for the rest of the match.
// - A "live pug" (positions, blocks) is any CTF match whose average team sizes
//   are equal whole numbers (upstream IsLivePug without the map_restart, AFK
//   and teamgen-reminded-position conditions); positions need 4v4 as upstream.
// - Location samples need only 3 s alive after a spawn (upstream also wanted
//   input within 10 s of real time; that is wall-clock and bots always act).
// - Time is counted in milliseconds of game time, not server frames.
// - Assists go to the assisting player (upstream credited the capper, against
//   its own help text), at each stock PERS_ASSIST_COUNT increment.
// - Selfkills count a death with no client killer (pits on your own, as the
//   help says) and not a team change.
// - Dropped: sqlite/accounts, pug stats, ratings and team balance, racemode,
//   aim practice, account "meme"/boost code, the TE LOS debug log
//   (d_logTEData), position debug prints, the Discord webhook and
//   SendMachineFriendlyStats, rocket surfing air shots (no WP_TouchRocket).

#include "g_local.h"
#include "be_collections.h"
#include "be_table.h"

/*
================================================================================
Types (upstream g_local.h)
================================================================================
*/

#define CTFPOSITION_MINIMUM_MS					(120 * 1000)	// 120 seconds minimum for pos detection
#define CTFPOSITION_MINIMUM_MS_LATEGAME			(240 * 1000)	// 240 seconds after the lategame threshold
#define CTFPOSITION_LATEGAME_THRESHOLD_MS		(600000)		// start using the lategame minimum after this long
#define CTFPOSITION_NOSWAPS_THRESHOLD_MS		(1200000)		// don't detect any swaps after this long
#define STATS_BLOCK_DURATION_MS					(300 * 1000)	// 5 minute blocks
#define CTF_SAVE_DISTANCE_THRESHOLD				(200)
#define CTFPOS_POSTSPAWN_DELAY_MS				(3000)			// wait after spawning before logging position data
#define AIRSHOT_GROUND_DISTANCE_THRESHOLD		(50.0f)
#define MAX_CAPTURES_TRACKED					(50)

typedef enum {
	CTFPOSITION_UNKNOWN = 0,
	CTFPOSITION_BASE,
	CTFPOSITION_CHASE,
	CTFPOSITION_OFFENSE
} ctfPosition_t;

typedef enum {
	CTFREGION_INVALID = -1,
	CTFREGION_FLAGSTAND = 0,
	CTFREGION_BASE,
	CTFREGION_MID,
	CTFREGION_ENEMYBASE,
	CTFREGION_ENEMYFLAGSTAND,
	NUM_CTFREGIONS
} ctfRegion_t;

typedef enum {
	MODC_INVALID = -1,
	MODC_FIRST = 0,
	MODC_MELEESTUNBATON = MODC_FIRST,
	MODC_SABER,
	MODC_PISTOL,
	MODC_BLASTER,
	MODC_DISRUPTOR,
	MODC_BOWCASTER,
	MODC_REPEATER,
	MODC_DEMP,
	MODC_GOLAN,
	MODC_ROCKET,
	MODC_CONCUSSION,
	MODC_THERMAL,
	MODC_MINE,
	MODC_DETPACK,
	MODC_FORCE,
	MODC_ALL_TYPES_COMBINED,
	MODC_MAX
} meansOfDeathCategory_t;

typedef enum {
	ACC_INVALID = -1,
	ACC_FIRST = 0,
	ACC_ALL_TYPES_COMBINED = ACC_FIRST,
	ACC_PISTOL_ALT,
	ACC_DISRUPTOR_PRIMARY = BE_ACC_DISRUPTOR_PRIMARY,
	ACC_DISRUPTOR_SNIPE = BE_ACC_DISRUPTOR_SNIPE,
	ACC_REPEATER_ALT,
	ACC_GOLAN_ALT,
	ACC_ROCKET,
	ACC_CONCUSSION_PRIMARY,
	ACC_CONCUSSION_ALT = BE_ACC_CONCUSSION_ALT,
	ACC_THERMAL_ALT,
	ACC_MAX
} accuracyCategory_t;

typedef enum {
	STATS_TABLE_GENERAL = 0,
	STATS_TABLE_FORCE,
	STATS_TABLE_DAMAGE,
	STATS_TABLE_WEAPON_GIVEN,
	STATS_TABLE_WEAPON_TAKEN,
	STATS_TABLE_ACCURACY,
	NUM_STATS_TABLES
} statsTableType_t;

// upstream stats_t; one per player, team and five-minute block
typedef struct beStats_s {
	beNode_t	node;

	int			blockNum;

	qboolean	isBot;
	qboolean	isTotal;			// the total row of a team (display only)
	int			sessionId;			// identity (be_stats identity list); same player, same id
	char		name[MAX_NETNAME];
	int			clientNum;			// to print in scoreboard order
	team_t		lastTeam;			// red or blue; a team change gets a new struct

	int			captures;
	int			assists;
	int			defends;
	int			accuracy_shots;
	int			accuracy_hits;
	int			accuracy_shotsOfType[ACC_MAX];
	int			accuracy_hitsOfType[ACC_MAX];
	int			accuracy;			// calculated on demand
	int			accuracyOfType[ACC_MAX];	// calculated on demand
	int			airs;
	int			teamKills;
	int			takes;
	int			pits;
	int			pitted;
	int			fcKills;
	int			fcKillsResultingInRets;
	int			fcKillEfficiency;	// calculated on demand
	int			rets;
	int			selfkills;
	int			boonPickups;
	int			healthPickedUp;
	int			armorPickedUp;
	int			totalFlagHold;
	int			longestFlagHold;
	int			saves;
	int			damageDealtTotal;
	int			damageTakenTotal;
	int			flagCarrierDamageDealtTotal;	// damage given to flag carriers
	int			flagCarrierDamageTakenTotal;	// damage received while carrying a flag
	int			clearDamageDealtTotal;			// damage given to non-carriers in your base/fs
	int			clearDamageTakenTotal;			// damage taken while not carrying, in the enemy base/fs
	int			otherDamageDealtTotal;
	int			otherDamageTakenTotal;
	int			damageOfType[MODC_MAX];			// only used for total rows

	float		topSpeed;
	float		displacement;		// units travelled while alive in game
	int			displacementMs;		// game time those units took (upstream displacementSamples / sv_fps)
	int			averageSpeed;		// calculated on demand

	beList_t	damageGivenList;	// damageCounter_t: whom this player damaged (including leavers)
	beList_t	damageTakenList;	// damageCounter_t: who damaged this player

	int			push;
	int			pull;
	int			healed;
	int			numEnergizes;
	float		normalizedEnergizeAmounts;	// two 90% efficient energizes = 1.8
	int			energizeEfficiency;	// calculated on demand
	int			energizedAlly;
	int			gotEnergizedByAlly;
	int			energizedEnemy;
	int			absorbed;
	int			protDamageAvoided;
	int			protTimeUsed;
	int			rageTimeUsed;
	int			drain;
	int			gotDrained;
	int			grips;
	int			gotGripped;
	int			forceSamples;
	int			darkForceSamples;
	int			darkPercent;		// calculated on demand

	beList_t	teammatePositioningList;	// ctfPositioningData_t: people I was in game with

	int			numFlagHolds;
	int			averageFlagHold;	// calculated on demand

	int			numGets;
	int			getTotalHealth;
	int			averageGetHealth;	// calculated on demand

	int			regionTime[NUM_CTFREGIONS];
	int			regionPercent[NUM_CTFREGIONS];
	float		totalLocationWithFlag;		// only 3+ seconds after spawning
	int			numLocationSamplesWithFlag;
	float		totalLocationWithoutFlag;
	int			numLocationSamplesWithoutFlag;
	int			msIngame;					// upstream ticksNotPaused, in game milliseconds
	int			numLocationSamplesRegardlessOfFlagHolding;
	int			confirmedPositionBits;		// every position this player played on this team

	ctfPosition_t	lastPosition;			// may be valid or unknown
	ctfPosition_t	finalPosition;			// set only when confirmed; overrides everything else
} beStats_t;

// upstream ctfPositioningData_t
typedef struct {
	beNode_t	node;
	beStats_t	*stats;
	int			msIngameWithMe;			// upstream numTicksIngameWithMe
	int			numLocationSamplesIngameWithMe;
	float		totalLocationWithFlagWithMe;
	int			numLocationSamplesWithFlagWithMe;
	float		totalLocationWithoutFlagWithMe;
	int			numLocationSamplesWithoutFlagWithMe;
} ctfPositioningData_t;

// upstream damageCounter_t
typedef struct {
	beNode_t	node;
	qboolean	otherPlayerIsBot;
	int			otherPlayerSessionId;
	beStats_t	*otherPlayerStats;
	int			totalAmount;
	int			ofType[MODC_MAX];
} damageCounter_t;

typedef struct {
	beNode_t	node;
	char		key[96];
	int			id;
} beStatsIdentity_t;

typedef struct {
	team_t		capturingTeam;
	int			time;
} beStatsCapture_t;

/*
================================================================================
State (upstream level.statsList, level.savedStatsList and friends)
================================================================================
*/

static beList_t		statsList;			// the ongoing stats_t of everyone who played this match
static beList_t		savedStatsList;		// archived five-minute blocks
static beList_t		identityList;
static int			nextSessionId;
static int			statBlock;

static beStats_t	*clientStats[MAX_CLIENTS];	// cache: the struct for the client's current team
static int			clientSessionId[MAX_CLIENTS];
static qboolean		clientIsBot[MAX_CLIENTS];
static qboolean		killedAlliedFlagCarrier[MAX_CLIENTS];
static qboolean		clientWasAlive[MAX_CLIENTS];
static int			clientSpawnTime[MAX_CLIENTS];
static int			lastHitscanHit[MAX_CLIENTS][ACC_MAX];

static beStats_t	*fcKiller[TEAM_NUM_TEAMS];	// [team]: who killed the carrier of team's flag (upstream redPlayerWhoKilledBlueCarrierOfRedFlag)

static int			numTeamTicks, numRedPlayerTicks, numBluePlayerTicks;
static qboolean		pugConfirmed;
static int			pugConfirmedPlayersPerTeam;

static beStatsCapture_t	captures[MAX_CAPTURES_TRACKED];
static int			numCaptures;

static qboolean		holdsFinalized;

static int			golanPairId[MAX_GENTITIES];	// upstream gentity_t::twin, kept here
static int			golanTwin[MAX_GENTITIES];
static int			nextGolanPairId;
static int			pendingGolanBall = -1, pendingGolanTime, pendingGolanOwner;
static int			directHitTime[MAX_GENTITIES];	// level.time + 1 when a missile hit a client directly
static int			teHighest;

static qboolean		flagstandsChecked, flagstandsValid;
static vec3_t		redFs, blueFs;
static float		diffBetweenFlags;

static beStats_t	bestStats[TEAM_NUM_TEAMS];	// bestStats[TEAM_RED].captures: best captures of anyone on red

#define BE_OUT_ALL		(-1)	// every human client, and the server console without colours
#define BE_OUT_CONSOLE	(-2)	// the server console (rcon reply), with colours
#define BE_OUT_LOG		(-3)	// the server console without colours

static qboolean BE_StatsActive( void )
{
	return ( g_beStats.integer && level.gametype == GT_CTF ) ? qtrue : qfalse;
}

static qboolean BE_HasFlag( gentity_t *ent )
{
	return ( ent && ent->client && ( ent->client->ps.powerups[PW_REDFLAG] || ent->client->ps.powerups[PW_BLUEFLAG] ||
		ent->client->ps.powerups[PW_NEUTRALFLAG] ) ) ? qtrue : qfalse;
}

static void BE_StatsFreeLists( beStats_t *s )
{
	BE_ListClear( &s->damageGivenList );
	BE_ListClear( &s->damageTakenList );
	BE_ListClear( &s->teammatePositioningList );
}

static void BE_StatsClearStatsList( beList_t *list )
{
	beIter_t iter;

	BE_ListIterate( list, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		BE_StatsFreeLists( (beStats_t *)BE_IterNext( &iter ) );
	}
	BE_ListClear( list );
}

// StatsValid: keeps people who join and immediately go spec off the tables
static qboolean StatsValid( const beStats_t *stats )
{
	if ( stats->isBot || stats->displacementMs >= 1000 || stats->damageDealtTotal || stats->damageTakenTotal )
	{
		return qtrue;
	}
	return qfalse;
}

/*
================================================================================
Identity and per-client lookup (upstream InitClientStats / ChangeStatsTeam)
================================================================================
*/

static qboolean BE_StatsIdentityMatches( void *element, void *userData )
{
	return !Q_stricmp( ( (beStatsIdentity_t *)element )->key, (const char *)userData ) ? qtrue : qfalse;
}

// ClientConnect: who this slot is now. Always kept (memory only), so turning
// g_beStats on mid-match finds every player.
void BE_StatsClientConnect( int clientNum, const char *guid, qboolean isBot )
{
	char name[MAX_NETNAME], key[96];
	beStatsIdentity_t *identity;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS )
	{
		return;
	}
	clientStats[clientNum] = NULL;
	killedAlliedFlagCarrier[clientNum] = qfalse;
	clientWasAlive[clientNum] = qfalse;
	clientIsBot[clientNum] = isBot;

	Q_strncpyz( name, level.clients[clientNum].pers.netname, sizeof( name ) );
	Q_StripColor( name );
	if ( isBot )
	{
		Com_sprintf( key, sizeof( key ), "bot:%d:%s", clientNum, name );
	}
	else if ( VALIDSTRING( guid ) && Q_stricmp( guid, "NOGUID" ) )
	{
		Com_sprintf( key, sizeof( key ), "guid:%s", guid );
	}
	else
	{
		Com_sprintf( key, sizeof( key ), "name:%s", name );
	}

	identity = (beStatsIdentity_t *)BE_ListFind( &identityList, BE_StatsIdentityMatches, key );
	if ( !identity )
	{
		identity = (beStatsIdentity_t *)BE_ListAdd( &identityList, sizeof( beStatsIdentity_t ) );
		if ( !identity )
		{
			clientSessionId[clientNum] = 0;
			return;
		}
		Q_strncpyz( identity->key, key, sizeof( identity->key ) );
		identity->id = ++nextSessionId;
	}
	clientSessionId[clientNum] = identity->id;
}

static void ArchiveAndResetStatsBlock( beStats_t *ongoing, int newBlockNum );

typedef struct {
	int		sessionId;
	team_t	team;
} sessionAndTeam_t;

static qboolean SessionIdAndTeamMatchesStats( void *element, void *userData )
{
	beStats_t *existing = (beStats_t *)element;
	sessionAndTeam_t *thisGuy = (sessionAndTeam_t *)userData;

	return ( existing->sessionId == thisGuy->sessionId && existing->lastTeam == thisGuy->team ) ? qtrue : qfalse;
}

// the stats_t of a red/blue client for its current team, made on first use
// (upstream InitClientStats + ChangeStatsTeam: a player gets one per team)
static beStats_t *StatsForEnt( gentity_t *ent )
{
	int clientNum;
	team_t team;
	beStats_t *s;
	sessionAndTeam_t ctx;

	if ( !BE_StatsActive() || !ent || !ent->client )
	{
		return NULL;
	}
	clientNum = ent - g_entities;
	if ( clientNum < 0 || clientNum >= MAX_CLIENTS || !clientSessionId[clientNum] )
	{
		return NULL;
	}
	team = ent->client->sess.sessionTeam;
	if ( team != TEAM_RED && team != TEAM_BLUE )
	{
		return NULL;
	}

	s = clientStats[clientNum];
	if ( s && s->lastTeam == team && s->sessionId == clientSessionId[clientNum] )
	{
		return s;
	}
	if ( s && s->lastTeam != team )
	{
		killedAlliedFlagCarrier[clientNum] = qfalse;	// upstream SetTeam
	}

	ctx.sessionId = clientSessionId[clientNum];
	ctx.team = team;
	s = (beStats_t *)BE_ListFind( &statsList, SessionIdAndTeamMatchesStats, &ctx );
	if ( !s )
	{ // not yet tracked; add him to the list
		s = (beStats_t *)BE_ListAdd( &statsList, sizeof( beStats_t ) );
		if ( !s )
		{
			return NULL;
		}
		s->isBot = clientIsBot[clientNum];
		s->sessionId = ctx.sessionId;
		s->lastTeam = team;
		s->blockNum = statBlock;
	}
	else if ( s->blockNum != statBlock )
	{
		if ( StatsValid( s ) )
		{ // came back after acquiring valid stats, and the block changed
			ArchiveAndResetStatsBlock( s, statBlock );
		}
		else
		{
			s->blockNum = statBlock;
		}
	}
	s->clientNum = clientNum;
	Q_strncpyz( s->name, ent->client->pers.netname, sizeof( s->name ) );
	clientStats[clientNum] = s;
	return s;
}

static float BE_Distance2D( const vec3_t a, const vec3_t b )
{
	const float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrtf( dx * dx + dy * dy );
}

/*
================================================================================
Map geometry: flagstands, location value and regions (upstream GetCTFLocationValue, GetCTFRegion)
================================================================================
*/

// upstream G_ClosestEntity( origin, isRedFlagstand/isBlueFlagstand ), once per map
static void BE_StatsFindFlagstands( void )
{
	gentity_t *red = NULL, *blue = NULL;
	float redDist = 0, blueDist = 0;
	int i;

	if ( flagstandsChecked )
	{
		return;
	}
	flagstandsChecked = qtrue;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];
		float dist;

		if ( !ent->inuse || !ent->classname || ( ent->flags & FL_DROPPED_ITEM ) )
		{
			continue;
		}
		dist = VectorLengthSquared( ent->r.currentOrigin );
		if ( !Q_stricmp( ent->classname, "team_CTF_redflag" ) && ( !red || dist < redDist ) )
		{
			red = ent;
			redDist = dist;
		}
		else if ( !Q_stricmp( ent->classname, "team_CTF_blueflag" ) && ( !blue || dist < blueDist ) )
		{
			blue = ent;
			blueDist = dist;
		}
	}
	if ( red && blue )
	{
		VectorCopy( red->r.currentOrigin, redFs );
		VectorCopy( blue->r.currentOrigin, blueFs );
		diffBetweenFlags = BE_Distance2D( redFs, blueFs );
		flagstandsValid = qtrue;
	}
}

// GetCTFLocationValue: 0 on top of your flagstand, 1 on top of the enemy's
static float GetCTFLocationValue( gentity_t *ent )
{
	float *allyFs, *enemyFs;
	float rPrime[2], v[2], vv;

	BE_StatsFindFlagstands();
	if ( !flagstandsValid || !ent->client )
	{
		return 0;
	}
	if ( ent->client->sess.sessionTeam == TEAM_RED )
	{
		allyFs = redFs;
		enemyFs = blueFs;
	}
	else
	{
		allyFs = blueFs;
		enemyFs = redFs;
	}
	rPrime[0] = ent->r.currentOrigin[0] - allyFs[0];
	rPrime[1] = ent->r.currentOrigin[1] - allyFs[1];
	v[0] = enemyFs[0] - allyFs[0];
	v[1] = enemyFs[1] - allyFs[1];
	vv = v[0] * v[0] + v[1] * v[1];
	if ( !vv )
	{
		return 0;
	}
	return ( v[0] * rPrime[0] + v[1] * rPrime[1] ) / vv;
}

// GetCTFRegion: fifths of the map by the distance ratio to the two flagstands
static ctfRegion_t GetCTFRegion( gentity_t *ent )
{
	team_t team;
	float allyDist, enemyDist, diff;

	if ( level.gametype != GT_CTF || !ent || !ent->client ||
		( ent->client->sess.sessionTeam != TEAM_RED && ent->client->sess.sessionTeam != TEAM_BLUE ) )
	{
		return CTFREGION_INVALID;
	}
	BE_StatsFindFlagstands();
	if ( !flagstandsValid )
	{
		return CTFREGION_INVALID;
	}

	team = ent->client->sess.sessionTeam;
	allyDist = BE_Distance2D( ent->r.currentOrigin, team == TEAM_RED ? redFs : blueFs );
	enemyDist = BE_Distance2D( ent->r.currentOrigin, team == TEAM_RED ? blueFs : redFs );

	if ( allyDist == enemyDist )
	{
		return CTFREGION_MID;
	}
	if ( allyDist < enemyDist )
	{
		diff = allyDist / enemyDist;
		if ( enemyDist > diffBetweenFlags || diff <= 0.2f )
			return CTFREGION_FLAGSTAND;
		if ( diff <= 0.4f )
			return CTFREGION_BASE;
		return CTFREGION_MID;
	}
	diff = enemyDist / allyDist;
	if ( allyDist > diffBetweenFlags || diff <= 0.2f )
		return CTFREGION_ENEMYFLAGSTAND;
	if ( diff <= 0.4f )
		return CTFREGION_ENEMYBASE;
	return CTFREGION_MID;
}

/*
================================================================================
Live match and positions (upstream IsLivePug, DetermineCTFPosition)
================================================================================
*/

// IsLivePug: players per team if the average team sizes are equal whole
// numbers (2v2 or more), else 0; locked in at 18 minutes
static int LivePlayersPerTeam( int ofAtLeastThisMinutes )
{
	float avgRed, avgBlue;
	int avgRedInt, avgBlueInt, durationMins;
	qboolean isLive;

	if ( level.gametype != GT_CTF || !numTeamTicks )
	{
		return 0;
	}
	if ( pugConfirmed )
	{
		int i, ingame = 0;

		for ( i = 0; i < level.maxclients; i++ )
		{
			if ( level.clients[i].pers.connected == CON_CONNECTED &&
				( level.clients[i].sess.sessionTeam == TEAM_RED || level.clients[i].sess.sessionTeam == TEAM_BLUE ) )
			{
				ingame++;
			}
		}
		return ingame > 0 ? pugConfirmedPlayersPerTeam : 0;
	}

	avgRed = (float)numRedPlayerTicks / (float)numTeamTicks;
	avgBlue = (float)numBluePlayerTicks / (float)numTeamTicks;
	avgRedInt = (int)floorf( avgRed + 0.5f );
	avgBlueInt = (int)floorf( avgBlue + 0.5f );
	durationMins = ( level.time - level.startTime ) / 60000;

	isLive = ( !( ofAtLeastThisMinutes > 0 && durationMins < ofAtLeastThisMinutes ) &&
		avgRedInt == avgBlueInt &&
		avgRedInt + avgBlueInt >= 4 &&
		fabs( avgRed - avgRedInt ) < 0.1f &&
		fabs( avgBlue - avgBlueInt ) < 0.1f ) ? qtrue : qfalse;

	if ( durationMins >= 18 )
	{
		pugConfirmed = qtrue;
		pugConfirmedPlayersPerTeam = isLive ? avgRedInt : 0;
	}
	return isLive ? avgRedInt : 0;
}

typedef struct {
	float			average;
	int				numberOfSamples;
	ctfPosition_t	forcePos;
	int				energizes;
	int				gets;
	ctfPosition_t	lastPosition;
	int				idNum;
	beStats_t		*stats;
	qboolean		cannotBeChase;
} ctfPositioningDataQuick_t;

static int ComparePositioningDataAverages( const void *a, const void *b )
{
	const ctfPositioningDataQuick_t *aa = (const ctfPositioningDataQuick_t *)a;
	const ctfPositioningDataQuick_t *bb = (const ctfPositioningDataQuick_t *)b;
	return ( aa->average > bb->average ) - ( aa->average < bb->average );
}

static int MoveAfksToEnd( const void *a, const void *b )
{
	const ctfPositioningDataQuick_t *aa = (const ctfPositioningDataQuick_t *)a;
	const ctfPositioningDataQuick_t *bb = (const ctfPositioningDataQuick_t *)b;
	if ( aa->numberOfSamples < bb->numberOfSamples )
		return 1;
	if ( aa->numberOfSamples > bb->numberOfSamples )
		return -1;
	return 0;
}

// DetermineCTFPosition: 4v4 base/chase/offense from the average location
// relative to the flagstands (0 own, 1 enemy), sampled 3 s after spawning and
// while not carrying. The two highest averages are offense, the lowest base,
// chase in between; edge cases use last block's position, gets, energizes and
// fc damage. Can be called on any stats_t at any time, including old ones.
static ctfPosition_t DetermineCTFPosition( beStats_t *posGuy )
{
	ctfPositioningDataQuick_t data[MAX_CLIENTS + 1];
	ctfPositioningDataQuick_t *base, *chase, *offense1, *offense2;
	float posGuyAverage;
	int numTeammates = 0, index, i;
	beIter_t iter;
	ctfPosition_t pos;

	if ( !posGuy )
		return CTFPOSITION_UNKNOWN;
	if ( posGuy->finalPosition ) // once set, always the position for this stats_t
		return posGuy->finalPosition;
	if ( !numTeamTicks )
		return posGuy->lastPosition;
	if ( LivePlayersPerTeam( 0 ) != 4 ) // we only care about 4v4
		return posGuy->lastPosition;
	if ( ( level.time - level.startTime ) < CTFPOSITION_MINIMUM_MS || !StatsValid( posGuy ) ||
		( posGuy->lastTeam != TEAM_RED && posGuy->lastTeam != TEAM_BLUE ) )
		return posGuy->lastPosition;
	if ( posGuy->msIngame < CTFPOSITION_MINIMUM_MS ) // not in the current block for 120+ seconds
		return posGuy->lastPosition;
	if ( level.time - level.startTime >= CTFPOSITION_LATEGAME_THRESHOLD_MS && posGuy->msIngame < CTFPOSITION_MINIMUM_MS_LATEGAME )
		return posGuy->lastPosition;
	if ( level.time - level.startTime >= CTFPOSITION_NOSWAPS_THRESHOLD_MS ) // assume no swaps very late into the match
		return posGuy->lastPosition;
	if ( !posGuy->numLocationSamplesRegardlessOfFlagHolding )
		return posGuy->lastPosition;
	if ( !posGuy->numLocationSamplesWithoutFlag ) // held the flag for this entire block
		return CTFPOSITION_OFFENSE;

	posGuyAverage = posGuy->totalLocationWithoutFlag / (float)posGuy->numLocationSamplesWithoutFlag;

	// how many people i was in game with for 120+ seconds
	BE_ListIterate( &posGuy->teammatePositioningList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		ctfPositioningData_t *teammate = (ctfPositioningData_t *)BE_IterNext( &iter );
		if ( !StatsValid( teammate->stats ) || teammate->msIngameWithMe < CTFPOSITION_MINIMUM_MS )
			continue;
		if ( !teammate->numLocationSamplesIngameWithMe && !teammate->stats->lastPosition )
			continue;
		++numTeammates;
	}
	if ( numTeammates < 3 || numTeammates > MAX_CLIENTS )
		return posGuy->lastPosition;

	// our own guy first
	memset( data, 0, sizeof( data ) );
	data->average = posGuyAverage;
	data->numberOfSamples = INT_MAX;
	data->energizes = posGuy->numEnergizes;
	data->lastPosition = posGuy->lastPosition;
	data->idNum = 0;
	data->stats = posGuy;
	if ( posGuy->flagCarrierDamageDealtTotal < 100 )
		data->cannotBeChase = qtrue;
	index = 1;

	BE_ListIterate( &posGuy->teammatePositioningList, &iter );
	while ( BE_IterHasNext( &iter ) && index <= numTeammates )
	{
		ctfPositioningData_t *teammate = (ctfPositioningData_t *)BE_IterNext( &iter );
		ctfPositioningDataQuick_t *thisGuyData;

		if ( !StatsValid( teammate->stats ) || teammate->msIngameWithMe < CTFPOSITION_MINIMUM_MS )
			continue;
		if ( !teammate->numLocationSamplesIngameWithMe && !teammate->stats->lastPosition )
			continue;

		thisGuyData = data + index++;
		thisGuyData->numberOfSamples = teammate->numLocationSamplesIngameWithMe;
		thisGuyData->energizes = teammate->stats->numEnergizes;
		thisGuyData->gets = teammate->stats->numGets;
		thisGuyData->lastPosition = teammate->stats->lastPosition;
		thisGuyData->idNum = index;
		thisGuyData->stats = teammate->stats;

		if ( !teammate->numLocationSamplesWithoutFlagWithMe )
		{ // in game with me for 120+ seconds but carrying the whole block: offense
			thisGuyData->forcePos = CTFPOSITION_OFFENSE;
		}
		else
		{
			thisGuyData->average = teammate->totalLocationWithoutFlagWithMe / (float)teammate->numLocationSamplesWithoutFlagWithMe;
			if ( teammate->stats->flagCarrierDamageDealtTotal < 100 )
				thisGuyData->cannotBeChase = qtrue;
		}
	}

	// more than three teammates: keep the three we played with the most
	if ( numTeammates > 3 )
		qsort( data, numTeammates + 1, sizeof( ctfPositioningDataQuick_t ), MoveAfksToEnd );

	// people who stayed close to their flagstand sort lower
	qsort( data, 4, sizeof( ctfPositioningDataQuick_t ), ComparePositioningDataAverages );

	// anyone forced to a particular position (at most one)
	for ( i = 0; i < 4; i++ )
	{
		ctfPositioningDataQuick_t *d = data + i;
		if ( !d->forcePos )
			continue;
		if ( ( !i && d->forcePos == CTFPOSITION_BASE ) ||
			( i == 1 && d->forcePos == CTFPOSITION_CHASE ) ||
			( ( i == 2 || i == 3 ) && d->forcePos == CTFPOSITION_OFFENSE ) )
			continue;
		if ( d->forcePos == CTFPOSITION_BASE )
			d->average = -999;
		else if ( d->forcePos == CTFPOSITION_CHASE )
			d->average = ( data->average + ( data + 1 )->average ) / 2.0f;
		else if ( d->forcePos == CTFPOSITION_OFFENSE )
			d->average = 999;
		qsort( data, 4, sizeof( ctfPositioningDataQuick_t ), ComparePositioningDataAverages );
		break;
	}

	base = data + 0;
	chase = data + 1;
	offense1 = data + 2;
	offense2 = data + 3;

	// chase and base averages extremely close together
	if ( chase->average - base->average < 0.08f )
	{
		if ( chase->lastPosition == CTFPOSITION_BASE && base->lastPosition == CTFPOSITION_CHASE && !chase->forcePos && !base->forcePos )
		{
			chase = data + 0;
			base = data + 1;
		}
		else if ( !chase->lastPosition && !base->lastPosition && chase->energizes > base->energizes && !chase->forcePos && !base->forcePos )
		{
			chase = data + 0;
			base = data + 1;
		}
	}

	// chase and offense averages extremely close together
	if ( offense1->average - chase->average < 0.08f )
	{
		if ( offense2->average - chase->average < 0.08f )
		{ // also the other offense player
			if ( chase->lastPosition == CTFPOSITION_OFFENSE && offense1->lastPosition == CTFPOSITION_CHASE && !chase->forcePos && !offense1->forcePos )
			{
				chase = data + 2;
				offense1 = data + 1;
			}
			else if ( chase->lastPosition == CTFPOSITION_OFFENSE && offense2->lastPosition == CTFPOSITION_CHASE && !chase->forcePos && !offense2->forcePos )
			{
				chase = data + 3;
				offense2 = data + 1;
			}
			else
			{ // whoever has the fewest gets
				int lowestGets = 999999, lowestGetsIndex = -1;
				for ( i = 3; i > 0; i-- )
				{
					if ( ( data + i )->gets <= lowestGets )
					{
						lowestGets = ( data + i )->gets;
						lowestGetsIndex = i;
					}
				}
				if ( lowestGetsIndex == 2 && !chase->forcePos && !offense1->forcePos )
				{
					chase = data + 2;
					offense1 = data + 1;
				}
				else if ( lowestGetsIndex == 3 && !chase->forcePos && !offense2->forcePos )
				{
					chase = data + 3;
					offense2 = data + 1;
				}
			}
		}
		else
		{ // just these two players
			if ( chase->lastPosition == CTFPOSITION_OFFENSE && offense1->lastPosition == CTFPOSITION_CHASE && !chase->forcePos && !offense1->forcePos )
			{
				chase = data + 2;
				offense1 = data + 1;
			}
			else if ( offense1->gets < chase->gets && !chase->forcePos && !offense1->forcePos )
			{
				chase = data + 2;
				offense1 = data + 1;
			}
		}
	}

	// the supposed chase has extremely low fc damage
	if ( chase->cannotBeChase )
	{
		if ( !offense1->forcePos && offense1->stats->flagCarrierDamageDealtTotal >= 100 &&
			offense1->stats->flagCarrierDamageDealtTotal > offense2->stats->flagCarrierDamageDealtTotal )
		{
			chase = data + 2;
			offense1 = data + 1;
		}
		else if ( !offense2->forcePos && offense2->stats->flagCarrierDamageDealtTotal >= 100 )
		{
			chase = data + 3;
			offense2 = data + 1;
		}
	}

	// the supposed chase has way less fc damage than the supposed offense1
	if ( !offense1->cannotBeChase && offense1->stats->flagCarrierDamageDealtTotal >= 300 &&
		offense1->stats->flagCarrierDamageDealtTotal >= chase->stats->flagCarrierDamageDealtTotal * 4 )
	{
		chase = data + 2;
		offense1 = data + 1;
	}

	// our guy is the one with id number zero
	if ( !base->idNum )
		pos = CTFPOSITION_BASE;
	else if ( !chase->idNum )
		pos = CTFPOSITION_CHASE;
	else
		pos = CTFPOSITION_OFFENSE;
	return pos;
}

// FinalizeCTFPositions
static void FinalizeCTFPositions( void )
{
	beIter_t iter;

	BE_ListIterate( &statsList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beStats_t *s = (beStats_t *)BE_IterNext( &iter );
		s->finalPosition = DetermineCTFPosition( s );
	}
}

/*
================================================================================
Five-minute blocks (upstream ArchiveAndResetStatsBlock, ChangeToNextStatsBlockIfNeeded)
================================================================================
*/

// copies a current stats_t into the archive and clears it for the new block
static void ArchiveAndResetStatsBlock( beStats_t *ongoing, int newBlockNum )
{
	beStats_t *archive = (beStats_t *)BE_ListAdd( &savedStatsList, sizeof( beStats_t ) );
	beStats_t keep;
	void *prev, *next;

	if ( !archive )
	{
		return;
	}
	prev = archive->node.prev;
	next = archive->node.next;
	memcpy( archive, ongoing, sizeof( beStats_t ) );
	archive->node.prev = prev;
	archive->node.next = next;

	if ( ongoing->finalPosition )
		archive->confirmedPositionBits |= ( 1 << ongoing->finalPosition );
	archive->blockNum = statBlock;

	// the archive's lists still hold the ongoing struct's pointers: copy them
	memset( &archive->damageGivenList, 0, sizeof( beList_t ) );
	memset( &archive->damageTakenList, 0, sizeof( beList_t ) );
	memset( &archive->teammatePositioningList, 0, sizeof( beList_t ) );
	BE_ListCopy( &ongoing->damageGivenList, &archive->damageGivenList, sizeof( damageCounter_t ) );
	BE_ListCopy( &ongoing->damageTakenList, &archive->damageTakenList, sizeof( damageCounter_t ) );
	BE_ListCopy( &ongoing->teammatePositioningList, &archive->teammatePositioningList, sizeof( ctfPositioningData_t ) );
	BE_StatsFreeLists( ongoing );

	// clear the ongoing struct, keeping who it is
	keep = *ongoing;
	prev = ongoing->node.prev;
	next = ongoing->node.next;
	memset( ongoing, 0, sizeof( beStats_t ) );
	ongoing->node.prev = prev;
	ongoing->node.next = next;
	ongoing->sessionId = keep.sessionId;
	ongoing->isBot = keep.isBot;
	ongoing->clientNum = keep.clientNum;
	Q_strncpyz( ongoing->name, keep.name, sizeof( ongoing->name ) );
	ongoing->lastTeam = keep.lastTeam;
	ongoing->lastPosition = archive->finalPosition;
	ongoing->confirmedPositionBits = archive->confirmedPositionBits;
	if ( ongoing->lastPosition )
		ongoing->confirmedPositionBits |= ( 1 << ongoing->lastPosition );
	ongoing->blockNum = newBlockNum;
}

// CheckAccountsOfOldBlocks, without accounts: archives take the current name
static void SyncNamesOfOldBlocks( int ignoreBlockNum )
{
	beIter_t iter, iter2;

	BE_ListIterate( &savedStatsList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beStats_t *archive = (beStats_t *)BE_IterNext( &iter );
		if ( archive->blockNum == ignoreBlockNum )
			continue;
		BE_ListIterate( &statsList, &iter2 );
		while ( BE_IterHasNext( &iter2 ) )
		{
			beStats_t *ongoing = (beStats_t *)BE_IterNext( &iter2 );
			if ( ongoing->sessionId == archive->sessionId )
			{
				Q_strncpyz( archive->name, ongoing->name, sizeof( archive->name ) );
				break;
			}
		}
	}
}

// every five minutes of a live match each player's stats_t is archived and
// cleared, so the current one holds the current block only; the match total
// is the current one plus every archived block
static void ChangeToNextStatsBlockIfNeeded( void )
{
	int durationMins, newBlockNum;
	beIter_t iter;

	if ( level.intermissiontime || !numTeamTicks )
		return;
	durationMins = ( level.time - level.startTime ) / 60000;
	if ( durationMins >= 120 || !LivePlayersPerTeam( 0 ) )
		return;
	newBlockNum = ( level.time - level.startTime ) / STATS_BLOCK_DURATION_MS;
	if ( newBlockNum <= statBlock )
		return;

	FinalizeCTFPositions();
	BE_ListIterate( &statsList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		ArchiveAndResetStatsBlock( (beStats_t *)BE_IterNext( &iter ), newBlockNum );
	}
	if ( newBlockNum > 1 )
		SyncNamesOfOldBlocks( statBlock );
	statBlock = newBlockNum;
}

/*
================================================================================
Damage matrices (upstream GetDamageGivenStat and friends)
================================================================================
*/

static qboolean MatchesDamage( void *element, void *userData )
{
	const damageCounter_t *existing = (const damageCounter_t *)element;
	const beStats_t *thisGuy = (const beStats_t *)userData;

	return ( thisGuy->isBot == existing->otherPlayerIsBot && existing->otherPlayerSessionId == thisGuy->sessionId ) ? qtrue : qfalse;
}

static damageCounter_t *GetDamageCounter( beList_t *list, beStats_t *other )
{
	damageCounter_t *dmg = (damageCounter_t *)BE_ListFind( list, MatchesDamage, other );

	if ( !dmg )
	{
		dmg = (damageCounter_t *)BE_ListAdd( list, sizeof( damageCounter_t ) );
		if ( !dmg )
		{
			return NULL;
		}
		dmg->otherPlayerIsBot = other->isBot;
		dmg->otherPlayerStats = other;
		dmg->otherPlayerSessionId = other->sessionId;
	}
	return dmg;
}

// GetTotalDamageGivenStatOfType: attacker's damage to victim over every block on attacker's team
static int GetTotalDamageGivenStatOfType( beStats_t *attacker, beStats_t *victim, meansOfDeathCategory_t modc )
{
	damageCounter_t *dmg = (damageCounter_t *)BE_ListFind( &attacker->damageGivenList, MatchesDamage, victim );
	int total = dmg ? dmg->ofType[modc] : 0, i;
	beIter_t iter;

	for ( i = 0; i < 2; i++ )
	{
		BE_ListIterate( !i ? &statsList : &savedStatsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *s = (beStats_t *)BE_IterNext( &iter );
			if ( s == attacker || s->sessionId != attacker->sessionId || s->lastTeam != attacker->lastTeam || !StatsValid( s ) )
				continue;
			dmg = (damageCounter_t *)BE_ListFind( &s->damageGivenList, MatchesDamage, victim );
			if ( dmg )
				total += dmg->ofType[modc];
		}
	}
	return total;
}

// GetTotalDamageTakenStatOfType: victim's damage from attacker over every block on victim's team
static int GetTotalDamageTakenStatOfType( beStats_t *attacker, beStats_t *victim, meansOfDeathCategory_t modc )
{
	damageCounter_t *dmg = (damageCounter_t *)BE_ListFind( &victim->damageTakenList, MatchesDamage, attacker );
	int total = dmg ? dmg->ofType[modc] : 0, i;
	beIter_t iter;

	for ( i = 0; i < 2; i++ )
	{
		BE_ListIterate( !i ? &statsList : &savedStatsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *s = (beStats_t *)BE_IterNext( &iter );
			if ( s == victim || s->sessionId != victim->sessionId || s->lastTeam != victim->lastTeam || !StatsValid( s ) )
				continue;
			dmg = (damageCounter_t *)BE_ListFind( &s->damageTakenList, MatchesDamage, attacker );
			if ( dmg )
				total += dmg->ofType[modc];
		}
	}
	return total;
}

// MeansOfDeathCategoryForMeansOfDeath
static meansOfDeathCategory_t MeansOfDeathCategoryForMeansOfDeath( int mod )
{
	switch ( mod )
	{
	case MOD_MELEE: case MOD_STUN_BATON: case MOD_VEHICLE: return MODC_MELEESTUNBATON;
	case MOD_SABER: return MODC_SABER;
	case MOD_BRYAR_PISTOL: case MOD_BRYAR_PISTOL_ALT: return MODC_PISTOL;
	case MOD_BLASTER: case MOD_TURBLAST: return MODC_BLASTER;
	case MOD_DISRUPTOR: case MOD_DISRUPTOR_SNIPER: case MOD_DISRUPTOR_SPLASH: return MODC_DISRUPTOR;
	case MOD_BOWCASTER: return MODC_BOWCASTER;
	case MOD_REPEATER: case MOD_REPEATER_ALT: case MOD_REPEATER_ALT_SPLASH: return MODC_REPEATER;
	case MOD_DEMP2: case MOD_DEMP2_ALT: return MODC_DEMP;
	case MOD_FLECHETTE: case MOD_FLECHETTE_ALT_SPLASH: return MODC_GOLAN;
	case MOD_ROCKET: case MOD_ROCKET_HOMING: case MOD_ROCKET_HOMING_SPLASH: case MOD_ROCKET_SPLASH: return MODC_ROCKET;
	case MOD_CONC: case MOD_CONC_ALT: return MODC_CONCUSSION;
	case MOD_THERMAL: case MOD_THERMAL_SPLASH: return MODC_THERMAL;
	case MOD_TRIP_MINE_SPLASH: case MOD_TIMED_MINE_SPLASH: return MODC_MINE;
	case MOD_DET_PACK_SPLASH: return MODC_DETPACK;
	case MOD_FORCE_DARK: return MODC_FORCE;
	default: return MODC_INVALID;
	}
}

/*
================================================================================
Hooks: deaths and damage (upstream g_combat.c)
================================================================================
*/

// player_die (and CheckAlmostCapture's save, which xenjk compiles out): saves,
// selfkills, pits, the hold of a carrier who died, teamkills and the return
// credited when a killed carrier's flag goes home at once
void BE_StatsPlayerDie( gentity_t *self, gentity_t *attacker, int meansOfDeath )
{
	beStats_t *s = StatsForEnt( self ), *as = NULL;
	team_t team;

	if ( !s )
	{
		return;
	}
	team = self->client->sess.sessionTeam;
	if ( attacker && attacker->client && attacker != self && attacker - g_entities < MAX_CLIENTS )
	{
		as = StatsForEnt( attacker );
	}

	// CheckAlmostCapture: a carrier killed very close to his own flagstand
	if ( as && BE_HasFlag( self ) )
	{
		const char *classname = ( team == TEAM_BLUE ) ? "team_CTF_blueflag" : "team_CTF_redflag";
		gentity_t *flag = NULL;
		vec3_t dir;

		do
		{
			flag = G_Find( flag, FOFS( classname ), classname );
		} while ( flag && ( flag->flags & FL_DROPPED_ITEM ) );
		if ( flag && !( flag->r.svFlags & SVF_NOCLIENT ) )
		{
			VectorSubtract( self->client->ps.origin, flag->s.origin, dir );
			if ( VectorLength( dir ) < CTF_SAVE_DISTANCE_THRESHOLD )
			{
				++as->saves;
			}
		}
	}

	if ( ( !attacker || attacker == self || !attacker->client ) && meansOfDeath != MOD_TEAM_CHANGE )
	{
		++s->selfkills;
	}

	if ( ( meansOfDeath == MOD_CRUSH || meansOfDeath == MOD_FALLING || meansOfDeath == MOD_TRIGGER_HURT ||
		meansOfDeath == MOD_UNKNOWN || meansOfDeath == MOD_SUICIDE ) && as && as->lastTeam == OtherTeam( team ) )
	{
		++s->pitted;
		++as->pits;
	}

	// longest flag hold
	if ( self->client->ps.powerups[PW_BLUEFLAG] || self->client->ps.powerups[PW_REDFLAG] )
	{
		const int thisFlaghold = level.time - self->client->pers.teamState.flagsince;
		s->totalFlagHold += thisFlaghold;
		if ( thisFlaghold > s->longestFlagHold )
			s->longestFlagHold = thisFlaghold;
	}

	if ( as && as->lastTeam == team )
	{
		as->teamKills++;
		if ( BE_HasFlag( self ) ) // refunded if the killer's team takes the flag
			killedAlliedFlagCarrier[attacker - g_entities] = qtrue;
	}

	// the flag goes straight home on a suicide or a fall to death
	if ( as && as->lastTeam == OtherTeam( team ) && ( self->client->ps.powerups[PW_REDFLAG] || self->client->ps.powerups[PW_BLUEFLAG] ) &&
		( meansOfDeath == MOD_SUICIDE || self->client->ps.fallingToDeath ) )
	{
		as->rets++;
	}
}

// G_Damage, protect: damage avoided against enemy attacks
void BE_StatsProtect( gentity_t *targ, gentity_t *attacker, int mod, int avoided )
{
	beStats_t *ts;

	if ( !attacker || !attacker->client || !targ || !targ->client || mod <= MOD_UNKNOWN || mod > MOD_FORCE_DARK ||
		attacker->client->sess.sessionTeam == targ->client->sess.sessionTeam )
	{
		return;
	}
	ts = StatsForEnt( targ );
	if ( ts )
	{
		ts->protDamageAvoided += avoided;
	}
}

// G_Damage: client-to-client damage, capped to the target's health + armour
void BE_StatsDamage( gentity_t *targ, gentity_t *attacker, int mod, int take, int asave )
{
	beStats_t *ts, *as;
	int damageStatIncrease;
	meansOfDeathCategory_t modc;
	damageCounter_t *dmg;

	if ( !attacker || !attacker->client || !targ || !targ->client || targ->health <= 0 ||
		mod <= MOD_UNKNOWN || mod > MOD_FORCE_DARK )
	{
		return;
	}
	ts = StatsForEnt( targ );
	as = StatsForEnt( attacker );
	if ( !ts || !as )
	{
		return;
	}

	damageStatIncrease = take + asave;
	if ( damageStatIncrease > targ->health + targ->client->ps.stats[STAT_ARMOR] )
		damageStatIncrease = targ->health + targ->client->ps.stats[STAT_ARMOR];

	if ( attacker->client->sess.sessionTeam != targ->client->sess.sessionTeam )
	{
		ts->damageTakenTotal += damageStatIncrease;
		as->damageDealtTotal += damageStatIncrease;
		if ( BE_HasFlag( targ ) )
		{
			ts->flagCarrierDamageTakenTotal += damageStatIncrease;
			as->flagCarrierDamageDealtTotal += damageStatIncrease;
		}
		else
		{
			ctfRegion_t targRegion = GetCTFRegion( targ );
			if ( targRegion == CTFREGION_ENEMYBASE || targRegion == CTFREGION_ENEMYFLAGSTAND )
			{
				ts->clearDamageTakenTotal += damageStatIncrease;
				as->clearDamageDealtTotal += damageStatIncrease;
			}
			else
			{
				ts->otherDamageTakenTotal += damageStatIncrease;
				as->otherDamageDealtTotal += damageStatIncrease;
			}
		}
	}

	modc = MeansOfDeathCategoryForMeansOfDeath( mod );
	dmg = GetDamageCounter( &as->damageGivenList, ts );
	if ( dmg )
	{
		dmg->totalAmount += damageStatIncrease;
		if ( modc != MODC_INVALID )
			dmg->ofType[modc] += damageStatIncrease;
	}
	dmg = GetDamageCounter( &ts->damageTakenList, as );
	if ( dmg )
	{
		dmg->totalAmount += damageStatIncrease;
		if ( modc != MODC_INVALID )
			dmg->ofType[modc] += damageStatIncrease;
	}
}

/*
================================================================================
Hooks: flags (upstream g_team.c)
================================================================================
*/

// Team_FragBonuses: the attacker fragged the carrier of his team's flag
void BE_StatsFcKill( gentity_t *attacker )
{
	beStats_t *as = StatsForEnt( attacker );

	if ( !as )
	{
		return;
	}
	as->fcKills++;
	fcKiller[as->lastTeam] = as;
}

// Team_FragBonuses: base, carrier and carrier-danger defends
void BE_StatsDefend( gentity_t *attacker )
{
	beStats_t *as = StatsForEnt( attacker );

	if ( as )
	{
		as->defends++;
	}
}

// Team_ResetFlag: team's flag is home again
void BE_StatsFlagReset( int team )
{
	int i;

	if ( team != TEAM_RED && team != TEAM_BLUE )
	{
		return;
	}
	fcKiller[team] = NULL;
	for ( i = 0; i < MAX_CLIENTS; i++ )
	{ // reset allied fc kills
		if ( g_entities[i].inuse && g_entities[i].client && g_entities[i].client->sess.sessionTeam == OtherTeam( team ) )
			killedAlliedFlagCarrier[i] = qfalse;
	}
}

// Team_TouchOurFlag: returned a dropped flag
void BE_StatsFlagReturned( gentity_t *returner, int team )
{
	beStats_t *s = StatsForEnt( returner );

	if ( !s )
	{
		return;
	}
	s->rets++;
	if ( team == s->lastTeam && fcKiller[team] )
	{
		fcKiller[team]->fcKillsResultingInRets++;
		fcKiller[team] = NULL;
	}
}

// Team_TouchOurFlag: captured
void BE_StatsCapture( gentity_t *capper )
{
	beStats_t *s = StatsForEnt( capper );
	int thisFlaghold;

	if ( !s )
	{
		return;
	}
	thisFlaghold = level.time - capper->client->pers.teamState.flagsince;
	s->totalFlagHold += thisFlaghold;
	if ( thisFlaghold > s->longestFlagHold )
		s->longestFlagHold = thisFlaghold;
	s->captures++;
	fcKiller[TEAM_RED] = fcKiller[TEAM_BLUE] = NULL;
	if ( numCaptures < MAX_CAPTURES_TRACKED )
	{
		captures[numCaptures].capturingTeam = s->lastTeam;
		captures[numCaptures].time = level.time - level.startTime;
		numCaptures++;
	}
}

// Team_TouchOurFlag: the assisting player (see the header comment)
void BE_StatsAssist( gentity_t *player )
{
	beStats_t *s = StatsForEnt( player );

	if ( s )
	{
		s->assists++;
	}
}

// Team_TouchEnemyFlag: holds, gets (and the health and armour at the get),
// the teamkill refund and TAKE for picking up a flag dropped by a teamkilled
// carrier, and CheckGetFlagSave
void BE_StatsFlagTaken( gentity_t *taker, gentity_t *flag, int team )
{
	beStats_t *s = StatsForEnt( taker );
	team_t takerTeam;
	int i;

	if ( !s || !flag )
	{
		return;
	}
	takerTeam = taker->client->sess.sessionTeam;
	++s->numFlagHolds;
	if ( team == TEAM_RED || team == TEAM_BLUE )
	{
		fcKiller[team] = NULL;
	}

	if ( !( flag->flags & FL_DROPPED_ITEM ) )
	{
		gentity_t *enemyCarrier = NULL;
		int alliedFlag = ( takerTeam == TEAM_RED ) ? PW_REDFLAG : PW_BLUEFLAG;

		++s->numGets;
		s->getTotalHealth += ( taker->health + taker->client->ps.stats[STAT_ARMOR] );

		// CheckGetFlagSave: the enemy carrier was about to capture
		for ( i = 0; i < MAX_CLIENTS; i++ )
		{
			gentity_t *thisGuy = &g_entities[i];
			if ( !thisGuy->inuse || !thisGuy->client || thisGuy->client->pers.connected != CON_CONNECTED ||
				thisGuy->client->sess.sessionTeam != OtherTeam( takerTeam ) || !thisGuy->client->ps.powerups[alliedFlag] )
				continue;
			enemyCarrier = thisGuy;
			break;
		}
		if ( enemyCarrier && Distance( flag->r.currentOrigin, enemyCarrier->r.currentOrigin ) < CTF_SAVE_DISTANCE_THRESHOLD )
		{
			++s->saves;
		}
	}
	else
	{ // a dropped flag: refund the fc teamkill
		qboolean gotFcTeamkiller = qfalse;

		for ( i = 0; i < MAX_CLIENTS; i++ )
		{
			gentity_t *teammate = &g_entities[i];
			if ( !teammate->inuse || !teammate->client || teammate->client->sess.sessionTeam != takerTeam )
				continue;
			if ( killedAlliedFlagCarrier[i] && !gotFcTeamkiller )
			{
				beStats_t *ts = StatsForEnt( teammate );
				if ( ts )
				{
					--ts->teamKills;
					if ( teammate == taker )
						++ts->takes;
				}
				gotFcTeamkiller = qtrue;
			}
			killedAlliedFlagCarrier[i] = qfalse;
		}
	}
}

/*
================================================================================
Hooks: items (upstream g_items.c)
================================================================================
*/

// Pickup_Powerup: boons that were not dropped
void BE_StatsPowerup( gentity_t *other, gentity_t *item )
{
	beStats_t *s;

	if ( !item || !item->item || item->item->giTag != PW_FORCE_BOON || item->s.modelindex2 )
	{
		return;
	}
	s = StatsForEnt( other );
	if ( s )
	{
		++s->boonPickups;
	}
}

// Pickup_Health: the health actually gained
void BE_StatsHealthPickup( gentity_t *other, int quantity, int max )
{
	beStats_t *s = StatsForEnt( other );
	int post;

	if ( !s )
	{
		return;
	}
	post = other->health + quantity;
	if ( post > max )
		post = max;
	if ( post > other->health )
		s->healthPickedUp += post - other->health;
}

// Pickup_Armor: the armour actually gained
void BE_StatsArmorPickup( gentity_t *other, int quantity, int max )
{
	beStats_t *s = StatsForEnt( other );
	int pre, post;

	if ( !s )
	{
		return;
	}
	pre = other->client->ps.stats[STAT_ARMOR];
	post = pre + quantity;
	if ( post > max )
		post = max;
	if ( post > pre )
		s->armorPickedUp += post - pre;
}

/*
================================================================================
Hooks: accuracy and air shots (upstream g_weapon.c, g_missile.c)
================================================================================
*/

// FireWeapon: shots that count for accuracy
void BE_StatsShot( gentity_t *ent, qboolean altFire )
{
	beStats_t *s = StatsForEnt( ent );
	accuracyCategory_t acc = ACC_INVALID;

	if ( !s )
	{
		return;
	}
	switch ( ent->s.weapon )
	{
	case WP_DISRUPTOR: acc = altFire ? ACC_DISRUPTOR_SNIPE : ACC_DISRUPTOR_PRIMARY; break;
	case WP_REPEATER: if ( altFire ) acc = ACC_REPEATER_ALT; break;
	case WP_FLECHETTE: if ( altFire ) acc = ACC_GOLAN_ALT; break;
	case WP_ROCKET_LAUNCHER: acc = ACC_ROCKET; break;
	case WP_THERMAL: if ( altFire ) acc = ACC_THERMAL_ALT; break;
	case WP_CONCUSSION: acc = altFire ? ACC_CONCUSSION_ALT : ACC_CONCUSSION_PRIMARY; break;
	default: break;
	}
	if ( acc != ACC_INVALID )
	{
		s->accuracy_shots++;
		s->accuracy_shotsOfType[acc]++;
	}
}

// WP_FireBryarPistol: a charged alt shot (pistol is not in overall accuracy)
void BE_StatsPistolCharge( gentity_t *ent, int count )
{
	beStats_t *s;

	if ( count <= 1 )
	{
		return;
	}
	s = StatsForEnt( ent );
	if ( s )
	{
		s->accuracy_shotsOfType[ACC_PISTOL_ALT]++;
	}
}

static void BE_StatsAddHit( beStats_t *s, accuracyCategory_t acc )
{
	if ( acc == ACC_INVALID )
	{
		return;
	}
	if ( acc != ACC_PISTOL_ALT ) // pistol does not count in overall accuracy
		s->accuracy_hits++;
	s->accuracy_hitsOfType[acc]++;
}

// disruptor primary/snipe (once per shot) and concussion alt (per enemy hit)
void BE_StatsHitscan( gentity_t *ent, int category )
{
	beStats_t *s = StatsForEnt( ent );
	int clientNum;

	if ( !s || category <= ACC_ALL_TYPES_COMBINED || category >= ACC_MAX )
	{
		return;
	}
	clientNum = ent - g_entities;
	if ( category == ACC_DISRUPTOR_PRIMARY || category == ACC_DISRUPTOR_SNIPE )
	{
		if ( lastHitscanHit[clientNum][category] == level.time + 1 )
			return; // this shot already hit
		lastHitscanHit[clientNum][category] = level.time + 1;
	}
	BE_StatsAddHit( s, (accuracyCategory_t)category );
}

// WP_CreateFlechetteBouncyThing: pairs the two balls of one alt shot so only
// the first to hit counts (upstream gentity_t::twin)
void BE_StatsGolanBall( gentity_t *ball )
{
	int num, owner;

	if ( !ball )
	{
		return;
	}
	num = ball->s.number;
	golanPairId[num] = 0;
	owner = ball->parent ? ball->parent->s.number : -1;
	if ( pendingGolanBall >= 0 && pendingGolanBall != num && pendingGolanTime == level.time && pendingGolanOwner == owner &&
		g_entities[pendingGolanBall].inuse && g_entities[pendingGolanBall].methodOfDeath == MOD_FLECHETTE_ALT_SPLASH )
	{
		golanPairId[num] = golanPairId[pendingGolanBall] = ++nextGolanPairId;
		golanTwin[num] = pendingGolanBall;
		golanTwin[pendingGolanBall] = num;
		pendingGolanBall = -1;
		return;
	}
	pendingGolanBall = num;
	pendingGolanTime = level.time;
	pendingGolanOwner = owner;
}

// AccuracyCategoryForProjectile; a golan ball's first hit uses up the pair
static accuracyCategory_t AccuracyCategoryForProjectile( gentity_t *projectile )
{
	if ( !projectile )
		return ACC_INVALID;
	if ( projectile->methodOfDeath == MOD_BRYAR_PISTOL_ALT && projectile->s.generic1 >= 2 )
		return ACC_PISTOL_ALT;
	if ( projectile->methodOfDeath == MOD_REPEATER_ALT )
		return ACC_REPEATER_ALT;
	if ( projectile->methodOfDeath == MOD_ROCKET || projectile->methodOfDeath == MOD_ROCKET_HOMING )
		return ACC_ROCKET;
	if ( projectile->methodOfDeath == MOD_THERMAL && !( projectile->flags & FL_BOUNCE_HALF ) )
		return ACC_THERMAL_ALT;
	if ( projectile->methodOfDeath == MOD_CONC )
		return ACC_CONCUSSION_PRIMARY;
	if ( projectile->methodOfDeath == MOD_FLECHETTE_ALT_SPLASH )
	{
		int num = projectile->s.number, id = golanPairId[num];

		if ( id )
		{ // neither ball has touched; count this one and never the other
			golanPairId[num] = 0;
			if ( golanPairId[golanTwin[num]] == id )
				golanPairId[golanTwin[num]] = 0;
			return ACC_GOLAN_ALT;
		}
		return ACC_INVALID;
	}
	return ACC_INVALID;
}

// CountsForAirshotStat
static qboolean CountsForAirshotStat( gentity_t *missile )
{
	switch ( missile->methodOfDeath )
	{
	case MOD_BOWCASTER:
	case MOD_REPEATER_ALT:
	case MOD_FLECHETTE_ALT_SPLASH:
	case MOD_ROCKET:
	case MOD_THERMAL:
	case MOD_CONC:
		return qtrue;
	case MOD_ROCKET_HOMING:
		return missile->enemy ? qfalse : qtrue; // alt rockets count if they are unhomed
	case MOD_BRYAR_PISTOL_ALT:
		return ( missile->s.generic1 > 1 ) ? qtrue : qfalse; // a little bit of charge
	default:
		return qfalse;
	}
}

// LogAccuracyHitSameTeamOkay
static qboolean LogAccuracyHitSameTeamOkay( gentity_t *target, gentity_t *attacker )
{
	if ( !target->takedamage || target == attacker || !target->client || !attacker || !attacker->client ||
		target->client->ps.stats[STAT_HEALTH] <= 0 )
	{
		return qfalse;
	}
	return qtrue;
}

// a deflected/reflected missile keeps its parent but changes owner
static qboolean BE_MissileReflected( gentity_t *missile )
{
	return ( missile->parent && missile->r.ownerNum != missile->parent->s.number ) ? qtrue : qfalse;
}

// CheckAccuracyAndAirshot: a missile hit a client directly (before the damage)
void BE_StatsMissileHit( gentity_t *missile, gentity_t *victim )
{
	gentity_t *missileOwner;
	beStats_t *s;
	accuracyCategory_t acc;

	if ( !BE_StatsActive() || !missile || !victim || missile->r.ownerNum >= MAX_CLIENTS )
	{
		return;
	}
	missileOwner = &g_entities[missile->r.ownerNum];
	if ( !missileOwner->inuse || !missileOwner->client || !LogAccuracyHitSameTeamOkay( victim, missileOwner ) )
	{
		return;
	}
	s = StatsForEnt( missileOwner );
	if ( !s )
	{
		return;
	}
	directHitTime[missile->s.number] = level.time + 1;	// the splash of this impact does not count again

	acc = AccuracyCategoryForProjectile( missile );
	if ( !OnSameTeam( victim, missileOwner ) && !BE_MissileReflected( missile ) )
	{
		BE_StatsAddHit( s, acc );
	}

	if ( victim->client->ps.groundEntityNum == ENTITYNUM_NONE && CountsForAirshotStat( missile ) )
	{ // hit in the air; make sure the victim is decently above the ground
		trace_t tr;
		vec3_t down;

		VectorCopy( victim->r.currentOrigin, down );
		down[2] -= 4096;
		trap->Trace( &tr, victim->r.currentOrigin, victim->r.mins, victim->r.maxs, down, victim->s.number, MASK_SOLID, qfalse, 0, 0 );
		VectorSubtract( victim->r.currentOrigin, tr.endpos, down );
		if ( VectorLength( down ) >= AIRSHOT_GROUND_DISTANCE_THRESHOLD && !OnSameTeam( victim, missileOwner ) )
		{
			++s->airs;
		}
	}
}

// G_ExplodeMissile, G_MissileImpact and the thermal explosion: the splash hit
// a client. ownerMustFire: the owner is r.ownerNum and a reflected missile
// does not count (upstream isReflected); otherwise owner is parent/activator.
void BE_StatsSplashHit( gentity_t *missile, gentity_t *owner, qboolean ownerMustFire )
{
	beStats_t *s;

	if ( !BE_StatsActive() || !missile || !owner || owner - g_entities < 0 || owner - g_entities >= MAX_CLIENTS || !owner->client )
	{
		return;
	}
	if ( ownerMustFire && ( BE_MissileReflected( missile ) || directHitTime[missile->s.number] == level.time + 1 ) )
	{
		return;
	}
	s = StatsForEnt( owner );
	if ( s )
	{
		BE_StatsAddHit( s, AccuracyCategoryForProjectile( missile ) );
	}
}

/*
================================================================================
Hooks: Force (upstream w_force.c)
================================================================================
*/

// ForceThrow: a push or pull that definitely happens
void BE_StatsPushPull( gentity_t *self, qboolean pull )
{
	beStats_t *s = StatsForEnt( self );

	if ( !s )
	{
		return;
	}
	if ( pull )
		++s->pull;
	else
		++s->push;
}

// ForceTeamHeal: health actually given to this ally
void BE_StatsTeamHeal( gentity_t *self, gentity_t *ally, int amount )
{
	beStats_t *s = StatsForEnt( self );
	int health, max;

	if ( !s || !ally || !ally->client )
	{
		return;
	}
	health = ally->client->ps.stats[STAT_HEALTH];
	max = ally->client->ps.stats[STAT_MAX_HEALTH];
	s->healed += ( health + amount > max ) ? ( max - health ) : amount;
}

// ForceTeamForceReplenish: Force actually given to this ally
void BE_StatsTeamEnergize( gentity_t *self, gentity_t *ally, int amount )
{
	beStats_t *s = StatsForEnt( self ), *as;
	int actual;

	if ( !s || !ally || !ally->client )
	{
		return;
	}
	actual = ( ally->client->ps.fd.forcePower + amount > ally->client->ps.fd.forcePowerMax ) ?
		( ally->client->ps.fd.forcePowerMax - ally->client->ps.fd.forcePower ) : amount;
	if ( actual < 0 )
		actual = 0;
	s->energizedAlly += actual;
	as = StatsForEnt( ally );
	if ( as )
		as->gotEnergizedByAlly += actual;
	if ( actual > teHighest )
		teHighest = actual;
}

// ForceTeamForceReplenish: one energize done; its efficiency is the most anyone got over the most anyone could
void BE_StatsTeamEnergizeDone( gentity_t *self, int amount )
{
	beStats_t *s = StatsForEnt( self );

	if ( s && amount > 0 )
	{
		++s->numEnergizes;
		s->normalizedEnergizeAmounts += Com_Clamp( 0.0f, 1.0f, (float)teHighest / (float)amount );
	}
	teHighest = 0;
}

// WP_AbsorbConversion: Force taken from an enemy's attack
void BE_StatsAbsorb( gentity_t *absorber, gentity_t *attacker, int amount )
{
	beStats_t *s, *as;
	int actual;

	if ( !absorber || !absorber->client || !attacker || !attacker->client ||
		attacker->client->sess.sessionTeam == absorber->client->sess.sessionTeam )
	{
		return;
	}
	s = StatsForEnt( absorber );
	as = StatsForEnt( attacker );
	if ( !s || !as )
	{
		return;
	}
	actual = ( absorber->client->ps.fd.forcePower + amount > absorber->client->ps.fd.forcePowerMax ) ?
		( absorber->client->ps.fd.forcePowerMax - absorber->client->ps.fd.forcePower ) : amount;
	if ( actual < 0 )
		actual = 0;
	s->absorbed += actual;
	as->energizedEnemy += actual;
}

// ForceGrip / BE_ForceGrip: a grip on an enemy starts
void BE_StatsGrip( gentity_t *self, gentity_t *victim )
{
	beStats_t *s, *vs;

	if ( !self || !self->client || !victim || !victim->client ||
		victim->client->sess.sessionTeam != OtherTeam( self->client->sess.sessionTeam ) )
	{
		return;
	}
	s = StatsForEnt( self );
	vs = StatsForEnt( victim );
	if ( s )
		s->grips++;
	if ( vs )
		vs->gotGripped++;
}

// ForceDrainDamage / BE_ForceDrainDamage: Force drained from an enemy
void BE_StatsDrain( gentity_t *self, gentity_t *victim, int amount )
{
	beStats_t *s, *vs;

	if ( !self || !self->client || !victim || !victim->client ||
		victim->client->sess.sessionTeam != OtherTeam( self->client->sess.sessionTeam ) )
	{
		return;
	}
	s = StatsForEnt( self );
	vs = StatsForEnt( victim );
	if ( s && vs )
	{
		s->drain += amount;
		vs->gotDrained += amount;
	}
}

/*
================================================================================
Frame (upstream G_RunFrame's speed stats and AddPlayerTick)
================================================================================
*/

static qboolean MatchesCtfPositioningData( void *element, void *userData )
{
	return ( (ctfPositioningData_t *)element )->stats == (beStats_t *)userData ? qtrue : qfalse;
}

// AddPlayerTick: time in game, location samples, and this player's samples in
// every teammate's record (so positions compare people who played together)
static void BE_StatsAddPlayerTick( gentity_t *ent, beStats_t *s, int frameMs, qboolean alive )
{
	qboolean validLocationSample, hasFlag;
	float loc = 0;
	int i;

	s->msIngame += frameMs;

	validLocationSample = ( alive && level.time - clientSpawnTime[ent - g_entities] >= CTFPOS_POSTSPAWN_DELAY_MS ) ? qtrue : qfalse;
	hasFlag = BE_HasFlag( ent );
	if ( validLocationSample )
	{
		loc = GetCTFLocationValue( ent );
		++s->numLocationSamplesRegardlessOfFlagHolding;
		if ( hasFlag )
		{
			s->totalLocationWithFlag += loc;
			++s->numLocationSamplesWithFlag;
		}
		else
		{
			s->totalLocationWithoutFlag += loc;
			++s->numLocationSamplesWithoutFlag;
		}
	}

	for ( i = 0; i < level.maxclients; i++ )
	{
		gentity_t *other = &g_entities[i];
		beStats_t *os;
		ctfPositioningData_t *data;

		if ( other == ent || !other->inuse || !other->client || other->client->pers.connected != CON_CONNECTED ||
			other->client->sess.sessionTeam != ent->client->sess.sessionTeam )
			continue;
		os = StatsForEnt( other );
		if ( !os )
			continue;
		data = (ctfPositioningData_t *)BE_ListFind( &os->teammatePositioningList, MatchesCtfPositioningData, s );
		if ( !data )
		{
			data = (ctfPositioningData_t *)BE_ListAdd( &os->teammatePositioningList, sizeof( ctfPositioningData_t ) );
			if ( !data )
				continue;
			data->stats = s;
		}
		data->msIngameWithMe += frameMs; // regardless of whether the position sample is valid
		if ( validLocationSample )
		{
			++data->numLocationSamplesIngameWithMe;
			if ( hasFlag )
			{
				data->totalLocationWithFlagWithMe += loc;
				++data->numLocationSamplesWithFlagWithMe;
			}
			else
			{
				data->totalLocationWithoutFlagWithMe += loc;
				++data->numLocationSamplesWithoutFlagWithMe;
			}
		}
	}
}

// G_RunFrame, after every client ran: time, speed, Force side, region time,
// protect/rage time and the position samples of every red/blue player
void BE_StatsFrame( void )
{
	int i, frameMs;

	if ( !BE_StatsActive() || level.intermissiontime )
	{
		return;
	}
	frameMs = level.time - level.previousTime;
	if ( frameMs < 0 )
		frameMs = 0;
	else if ( frameMs > 1000 )
		frameMs = 1000;

	ChangeToNextStatsBlockIfNeeded();

	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ )
	{
		gentity_t *ent = &g_entities[i];
		gclient_t *cl = ent->client;
		beStats_t *s;
		qboolean alive;

		if ( !ent->inuse || !cl || cl->pers.connected != CON_CONNECTED )
		{
			clientWasAlive[i] = qfalse;
			continue;
		}
		alive = ( cl->ps.stats[STAT_HEALTH] > 0 && !( cl->ps.eFlags & EF_DEAD ) ) ? qtrue : qfalse;
		if ( alive && !clientWasAlive[i] )
			clientSpawnTime[i] = level.time;
		clientWasAlive[i] = alive;

		if ( cl->sess.sessionTeam != TEAM_RED && cl->sess.sessionTeam != TEAM_BLUE )
		{
			killedAlliedFlagCarrier[i] = qfalse;	// upstream SetTeam to spectator
			continue;
		}
		if ( cl->ps.pm_flags & PMF_FOLLOW )
			continue;
		s = StatsForEnt( ent );
		if ( !s )
			continue;
		if ( strcmp( s->name, cl->pers.netname ) )
			Q_strncpyz( s->name, cl->pers.netname, sizeof( s->name ) );	// renamed

		if ( cl->sess.sessionTeam == TEAM_RED )
			numRedPlayerTicks++;
		else
			numBluePlayerTicks++;
		BE_StatsAddPlayerTick( ent, s, frameMs, alive );

		if ( alive )
		{ // in game and alive: speed stats
			float xyspeed;
			ctfRegion_t region;
			const vec_t *velocity = cl->ps.velocity;

			if ( cl->ps.m_iVehicleNum && g_entities[cl->ps.m_iVehicleNum].client )
				velocity = g_entities[cl->ps.m_iVehicleNum].client->ps.velocity;
			xyspeed = sqrtf( velocity[0] * velocity[0] + velocity[1] * velocity[1] );

			s->displacement += xyspeed * frameMs / 1000.0f;
			s->displacementMs += frameMs;
			s->forceSamples++;
			if ( cl->ps.fd.forceSide == FORCE_DARKSIDE )
				s->darkForceSamples++;
			region = GetCTFRegion( ent );
			if ( region != CTFREGION_INVALID )
				s->regionTime[region] += frameMs;
			if ( xyspeed > s->topSpeed )
				s->topSpeed = xyspeed;
			if ( cl->ps.fd.forcePowersActive & ( 1 << FP_PROTECT ) )
				s->protTimeUsed += frameMs;
			if ( cl->ps.fd.forcePowersActive & ( 1 << FP_RAGE ) )
				s->rageTimeUsed += frameMs;
		}
	}
	numTeamTicks++;
}

/*
================================================================================
Table cells (upstream CtfStatsTableCallback_*)
================================================================================
*/

// FormatStatInt: gold if best overall, green if best on team
static const char *FormatStatInt( qboolean isTotal, int num, int bestMyTeam, int bestOtherTeam )
{
	if ( isTotal )
		return va( "^9%d", num );
	if ( !num && !bestMyTeam )
		return va( "%d", num ); // everybody has zero on my team; no color
	if ( num >= bestMyTeam && num >= bestOtherTeam )
		return va( "^3%d", num );
	if ( num >= bestMyTeam )
		return va( "^2%d", num );
	return va( "%d", num );
}

// FormatStatTime
static const char *FormatStatTime( qboolean isTotal, int num, int bestMyTeam, int bestOtherTeam )
{
	int secs = num / 1000;
	int mins = secs / 60;
	const char *color = "";

	if ( isTotal )
		color = "^9";
	else if ( !num && !bestMyTeam )
		color = "";
	else if ( num >= bestMyTeam && num >= bestOtherTeam )
		color = "^3";
	else if ( num >= bestMyTeam )
		color = "^2";

	if ( num >= 60000 )
		return va( "%s%dm%02ds", color, mins, secs % 60 );
	return va( "%s%ds", color, secs );
}

#define BEST_MY( field )	bestStats[stats->lastTeam].field
#define BEST_OTHER( field )	bestStats[OtherTeam( stats->lastTeam )].field
#define STAT_INT_CELL( fn, field ) \
	static const char *fn( void *rowContext, void *columnContext ) { \
		beStats_t *stats = (beStats_t *)rowContext; \
		return FormatStatInt( stats->isTotal, stats->field, BEST_MY( field ), BEST_OTHER( field ) ); \
	}
#define STAT_TIME_CELL( fn, field ) \
	static const char *fn( void *rowContext, void *columnContext ) { \
		beStats_t *stats = (beStats_t *)rowContext; \
		return FormatStatTime( stats->isTotal, stats->field, BEST_MY( field ), BEST_OTHER( field ) ); \
	}
// cells that stay blank (but keep the column) when there is nothing to divide by
#define STAT_RATIO_CELL( fn, field, guard ) \
	static const char *fn( void *rowContext, void *columnContext ) { \
		beStats_t *stats = (beStats_t *)rowContext; \
		if ( !stats->guard ) return " "; \
		return FormatStatInt( stats->isTotal, stats->field, BEST_MY( field ), BEST_OTHER( field ) ); \
	}

STAT_INT_CELL( Cell_Captures, captures )
STAT_INT_CELL( Cell_Assists, assists )
STAT_INT_CELL( Cell_Defends, defends )
STAT_INT_CELL( Cell_Accuracy, accuracy )
STAT_INT_CELL( Cell_Airs, airs )
STAT_INT_CELL( Cell_TeamKills, teamKills )
STAT_INT_CELL( Cell_Takes, takes )
STAT_INT_CELL( Cell_Pits, pits )
STAT_INT_CELL( Cell_Pitted, pitted )
STAT_INT_CELL( Cell_FcKills, fcKills )
STAT_INT_CELL( Cell_Rets, rets )
STAT_INT_CELL( Cell_Selfkills, selfkills )
STAT_INT_CELL( Cell_BoonPickups, boonPickups )
STAT_TIME_CELL( Cell_TotalHold, totalFlagHold )
STAT_TIME_CELL( Cell_MaxHold, longestFlagHold )
STAT_INT_CELL( Cell_DamageDealt, damageDealtTotal )
STAT_INT_CELL( Cell_DamageTaken, damageTakenTotal )
STAT_INT_CELL( Cell_FlagCarrierDamageDealt, flagCarrierDamageDealtTotal )
STAT_INT_CELL( Cell_FlagCarrierDamageTaken, flagCarrierDamageTakenTotal )
STAT_INT_CELL( Cell_ClearDamageDealt, clearDamageDealtTotal )
STAT_INT_CELL( Cell_ClearDamageTaken, clearDamageTakenTotal )
STAT_INT_CELL( Cell_OtherDamageDealt, otherDamageDealtTotal )
STAT_INT_CELL( Cell_OtherDamageTaken, otherDamageTakenTotal )
STAT_INT_CELL( Cell_AverageSpeed, averageSpeed )
STAT_INT_CELL( Cell_Push, push )
STAT_INT_CELL( Cell_Pull, pull )
STAT_INT_CELL( Cell_Healed, healed )
STAT_INT_CELL( Cell_EnergizedAlly, energizedAlly )
STAT_RATIO_CELL( Cell_EnergizeEfficiency, energizeEfficiency, numEnergizes )
STAT_INT_CELL( Cell_GotEnergizedByAlly, gotEnergizedByAlly )
STAT_INT_CELL( Cell_EnergizedEnemy, energizedEnemy )
STAT_INT_CELL( Cell_Absorbed, absorbed )
STAT_INT_CELL( Cell_ProtDamage, protDamageAvoided )
STAT_TIME_CELL( Cell_ProtTime, protTimeUsed )
STAT_TIME_CELL( Cell_RageTime, rageTimeUsed )
STAT_INT_CELL( Cell_Drain, drain )
STAT_INT_CELL( Cell_GotDrained, gotDrained )
STAT_INT_CELL( Cell_Grips, grips )
STAT_INT_CELL( Cell_GotGripped, gotGripped )
STAT_RATIO_CELL( Cell_DarkPercent, darkPercent, forceSamples )
STAT_RATIO_CELL( Cell_FlagCarrierKillEfficiency, fcKillEfficiency, fcKills )

static const char *Cell_TopSpeed( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	return FormatStatInt( stats->isTotal, (int)( stats->topSpeed + 0.5f ), (int)( BEST_MY( topSpeed ) + 0.5f ), (int)( BEST_OTHER( topSpeed ) + 0.5f ) );
}

static const char *Cell_RegionPercent( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	int region = *(int *)columnContext;
	return FormatStatInt( stats->isTotal, stats->regionPercent[region], BEST_MY( regionPercent[region] ), BEST_OTHER( regionPercent[region] ) );
}

static const char *Cell_Name( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	return stats->isTotal ? "^9TOTAL" : stats->name;
}

static const char *Cell_Alias( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	return ( !stats->isTotal && stats->isBot ) ? "BOT" : NULL;
}

static const char *PosAbbreviation( ctfPosition_t pos )
{
	switch ( pos )
	{
	case CTFPOSITION_BASE: return "Bas";
	case CTFPOSITION_CHASE: return "Cha";
	case CTFPOSITION_OFFENSE: return "Off";
	default: return "";
	}
}

// GetPositionStringForStats: "Off ^9(Bas/Cha)" with older positions in brackets
static const char *GetPositionStringForStats( beStats_t *stats )
{
	char buf[64];
	ctfPosition_t pos, p;
	qboolean gotOldPos = qfalse;

	if ( !stats || stats->isTotal )
		return NULL;
	pos = stats->finalPosition ? stats->finalPosition : DetermineCTFPosition( stats );
	if ( !pos )
		return NULL;

	Q_strncpyz( buf, PosAbbreviation( pos ), sizeof( buf ) );
	for ( p = CTFPOSITION_BASE; p <= CTFPOSITION_OFFENSE; p++ )
	{
		if ( pos != p && ( stats->confirmedPositionBits & ( 1 << p ) ) )
		{
			Q_strcat( buf, sizeof( buf ), gotOldPos ? "/" : " ^9(" );
			Q_strcat( buf, sizeof( buf ), PosAbbreviation( p ) );
			gotOldPos = qtrue;
		}
	}
	if ( gotOldPos )
		Q_strcat( buf, sizeof( buf ), ")" );
	return va( "%s", buf );
}

static const char *Cell_Position( void *rowContext, void *columnContext )
{
	return GetPositionStringForStats( (beStats_t *)rowContext );
}

static const char *Cell_Time( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	int secs;

	if ( stats->isTotal )
		return NULL;
	secs = stats->msIngame / 1000;
	return va( "%d:%02d", secs / 60, secs % 60 );
}

// TableCallback_Damage: row player's damage to the column player
static const char *Cell_Damage( void *rowContext, void *columnContext )
{
	beStats_t *attacker = (beStats_t *)rowContext;
	beStats_t *victim = (beStats_t *)columnContext;
	damageCounter_t *damage = NULL, *bestOnTeam = NULL, *bestOnOtherTeam = NULL;
	beIter_t iter;

	if ( !attacker || !victim )
		return NULL;

	BE_ListIterate( &attacker->damageGivenList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		damageCounter_t *thisGuy = (damageCounter_t *)BE_IterNext( &iter );
		if ( thisGuy->otherPlayerSessionId == victim->sessionId )
		{
			damage = thisGuy;
			break;
		}
	}
	if ( !damage )
		return attacker->isTotal ? "^90" : "0";
	if ( attacker->isTotal )
		return FormatStatInt( qtrue, damage->totalAmount, 0, 0 );

	BE_ListIterate( &bestStats[attacker->lastTeam].damageGivenList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		damageCounter_t *thisGuy = (damageCounter_t *)BE_IterNext( &iter );
		if ( thisGuy->otherPlayerSessionId == victim->sessionId )
		{
			bestOnTeam = thisGuy;
			break;
		}
	}
	BE_ListIterate( &bestStats[OtherTeam( attacker->lastTeam )].damageGivenList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		damageCounter_t *thisGuy = (damageCounter_t *)BE_IterNext( &iter );
		if ( thisGuy->otherPlayerSessionId == victim->sessionId )
		{
			bestOnOtherTeam = thisGuy;
			break;
		}
	}
	return FormatStatInt( qfalse, damage->totalAmount, bestOnTeam ? bestOnTeam->totalAmount : 0, bestOnOtherTeam ? bestOnOtherTeam->totalAmount : 0 );
}

typedef struct {
	beStats_t				*tablePlayerStats;
	qboolean				damageTaken;
	meansOfDeathCategory_t	modc;
} meansOfDeathCategoryContext_t;

// TableCallback_WeaponDamage
static const char *Cell_WeaponDamage( void *rowContext, void *columnContext )
{
	beStats_t *rowPlayer = (beStats_t *)rowContext;
	meansOfDeathCategoryContext_t *context = (meansOfDeathCategoryContext_t *)columnContext;
	beStats_t *tablePlayer = context->tablePlayerStats;
	int damage = 0, i;

	if ( rowPlayer->isTotal )
	{
		if ( context->modc == MODC_ALL_TYPES_COMBINED )
		{
			for ( i = MODC_FIRST; i < MODC_ALL_TYPES_COMBINED; i++ )
				damage += rowPlayer->damageOfType[i];
		}
		else
		{
			damage = rowPlayer->damageOfType[context->modc];
		}
		return FormatStatInt( qtrue, damage, 0, 0 );
	}

	if ( context->modc == MODC_ALL_TYPES_COMBINED )
	{
		for ( i = MODC_FIRST; i < MODC_ALL_TYPES_COMBINED; i++ )
			damage += context->damageTaken ? GetTotalDamageTakenStatOfType( rowPlayer, tablePlayer, (meansOfDeathCategory_t)i ) :
				GetTotalDamageGivenStatOfType( tablePlayer, rowPlayer, (meansOfDeathCategory_t)i );
	}
	else
	{
		damage = context->damageTaken ? GetTotalDamageTakenStatOfType( rowPlayer, tablePlayer, context->modc ) :
			GetTotalDamageGivenStatOfType( tablePlayer, rowPlayer, context->modc );
	}
	return FormatStatInt( qfalse, damage, bestStats[rowPlayer->lastTeam].damageOfType[context->modc],
		bestStats[OtherTeam( rowPlayer->lastTeam )].damageOfType[context->modc] );
}

// which weapons exist on this map (upstream level.existingWeaponSpawns, level.boonExists)
static int ExistingWeaponSpawns( qboolean *boonExists )
{
	int i, bits = 0;

	if ( boonExists )
		*boonExists = qfalse;
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ )
	{
		gentity_t *ent = &g_entities[i];
		if ( !ent->inuse || !ent->item || ( ent->flags & FL_DROPPED_ITEM ) )
			continue;
		if ( ent->item->giType == IT_WEAPON && ent->item->giTag > 0 && ent->item->giTag < 32 )
			bits |= ( 1 << ent->item->giTag );
		else if ( ent->item->giType == IT_POWERUP && ent->item->giTag == PW_FORCE_BOON && boonExists )
			*boonExists = qtrue;
	}
	return bits;
}

static int existingWeaponSpawns;

// ShouldShowAccuracyCategory: only columns for weapons that exist on this map
static qboolean ShouldShowAccuracyCategory( accuracyCategory_t acc )
{
	switch ( acc )
	{
	case ACC_PISTOL_ALT: case ACC_ALL_TYPES_COMBINED: return qtrue;
	case ACC_DISRUPTOR_PRIMARY: case ACC_DISRUPTOR_SNIPE: return ( existingWeaponSpawns & ( 1 << WP_DISRUPTOR ) ) ? qtrue : qfalse;
	case ACC_REPEATER_ALT: return ( existingWeaponSpawns & ( 1 << WP_REPEATER ) ) ? qtrue : qfalse;
	case ACC_GOLAN_ALT: return ( existingWeaponSpawns & ( 1 << WP_FLECHETTE ) ) ? qtrue : qfalse;
	case ACC_ROCKET: return ( existingWeaponSpawns & ( 1 << WP_ROCKET_LAUNCHER ) ) ? qtrue : qfalse;
	case ACC_CONCUSSION_PRIMARY: case ACC_CONCUSSION_ALT: return ( existingWeaponSpawns & ( 1 << WP_CONCUSSION ) ) ? qtrue : qfalse;
	case ACC_THERMAL_ALT: return ( existingWeaponSpawns & ( 1 << WP_THERMAL ) ) ? qtrue : qfalse;
	default: return qfalse;
	}
}

// CtfStatsTableCallback_WeaponAccuracy
static const char *Cell_WeaponAccuracy( void *rowContext, void *columnContext )
{
	beStats_t *stats = (beStats_t *)rowContext;
	accuracyCategory_t acc = *(accuracyCategory_t *)columnContext;

	if ( !ShouldShowAccuracyCategory( acc ) )
		return NULL;
	if ( acc == ACC_ALL_TYPES_COMBINED )
	{
		if ( !stats->accuracy_shots )
			return " "; // keep the column while nobody has fired
		return FormatStatInt( stats->isTotal, stats->accuracy, BEST_MY( accuracy ), BEST_OTHER( accuracy ) );
	}
	if ( !stats->accuracy_shotsOfType[acc] )
		return " ";
	return FormatStatInt( stats->isTotal, stats->accuracyOfType[acc], BEST_MY( accuracyOfType[acc] ), BEST_OTHER( accuracyOfType[acc] ) );
}

static const char *NameForMeansOfDeathCategory( meansOfDeathCategory_t modc )
{
	static const char *names[MODC_MAX] = { "^5Mel", "^5Sab", "^5Pis", "^5Bla", "^5Dis", "^5Bow", "^5Rep", "^5Dmp",
		"^5Gol", "^5Roc", "^5Con", "^5The", "^5Min", "^5Dpk", "^5For", "^5Total" };
	return ( modc >= MODC_FIRST && modc < MODC_MAX ) ? names[modc] : "";
}

static const char *NameForAccuracyCategory( accuracyCategory_t acc )
{
	static const char *names[ACC_MAX] = { "^5Acc", "^5Pis", "^5Dis", "^5Snp", "^5Rep", "^5Gol", "^5Roc", "^5Con", "^5Alt", "^5The" };
	return ( acc >= ACC_FIRST && acc < ACC_MAX ) ? names[acc] : "";
}

/*
================================================================================
Best and total rows (upstream CheckBestStats, AddStatsToTotal)
================================================================================
*/

static void SetRegionPercents( beStats_t *s )
{
	int allRegionsTime = 0, region;

	for ( region = CTFREGION_FLAGSTAND; region <= CTFREGION_ENEMYFLAGSTAND; region++ )
		allRegionsTime += s->regionTime[region];
	for ( region = CTFREGION_FLAGSTAND; region <= CTFREGION_ENEMYFLAGSTAND; region++ )
		s->regionPercent[region] = allRegionsTime ?
			Com_Clampi( 0, 100, (int)floorf( (float)s->regionTime[region] / (float)allRegionsTime * 100.0f + 0.5f ) ) : 0;
}

static void SetAverageSpeed( beStats_t *s )
{
	if ( s->displacementMs )
		s->averageSpeed = (int)floorf( s->displacement * 1000.0f / (float)s->displacementMs + 0.5f );
	else
		s->averageSpeed = (int)( s->topSpeed + 0.5f );
}

// the per-player fields only calculated on demand
static void SetDerivedStats( beStats_t *s )
{
	int i;

	s->accuracy = s->accuracy_shots ? s->accuracy_hits * 100 / s->accuracy_shots : 0;
	for ( i = ACC_FIRST; i < ACC_MAX; i++ )
		s->accuracyOfType[i] = s->accuracy_shotsOfType[i] ? s->accuracy_hitsOfType[i] * 100 / s->accuracy_shotsOfType[i] : 0;
	s->fcKillEfficiency = s->fcKills ? s->fcKillsResultingInRets * 100 / s->fcKills : 0;
	s->averageFlagHold = s->numFlagHolds ? s->totalFlagHold / s->numFlagHolds : 0;
	SetAverageSpeed( s );
	s->energizeEfficiency = s->numEnergizes ? (int)( s->normalizedEnergizeAmounts * 100 / s->numEnergizes ) : 0;
	s->darkPercent = s->forceSamples ? s->darkForceSamples * 100 / s->forceSamples : 0;
	SetRegionPercents( s );
	s->averageGetHealth = s->numGets ? (int)floorf( ( (float)s->getTotalHealth / s->numGets ) + 0.5f ) : 0;
}

#define CheckBest( field )	do { if ( player->field > best->field ) { best->field = player->field; } } while ( 0 )

// CheckBestStats: runs on every row to set up bestStats
static void CheckBestStats( beStats_t *player, statsTableType_t type, beStats_t *weaponStatsPtr )
{
	beStats_t *best = &bestStats[player->lastTeam];
	int i;

	SetDerivedStats( player );
	if ( type == STATS_TABLE_GENERAL )
	{
		CheckBest( captures );
		CheckBest( assists );
		CheckBest( defends );
		CheckBest( accuracy );
		CheckBest( airs );
		CheckBest( pits );
		CheckBest( pitted );
		CheckBest( fcKills );
		CheckBest( fcKillEfficiency );
		CheckBest( flagCarrierDamageDealtTotal );
		CheckBest( flagCarrierDamageTakenTotal );
		CheckBest( clearDamageDealtTotal );
		CheckBest( clearDamageTakenTotal );
		CheckBest( otherDamageDealtTotal );
		CheckBest( otherDamageTakenTotal );
		CheckBest( teamKills );
		CheckBest( takes );
		CheckBest( rets );
		CheckBest( selfkills );
		CheckBest( healthPickedUp );
		CheckBest( armorPickedUp );
		CheckBest( totalFlagHold );
		CheckBest( longestFlagHold );
		CheckBest( numFlagHolds );
		CheckBest( averageFlagHold );
		CheckBest( saves );
		CheckBest( damageDealtTotal );
		CheckBest( damageTakenTotal );
		CheckBest( topSpeed );
		CheckBest( averageSpeed );
	}
	else if ( type == STATS_TABLE_FORCE )
	{
		CheckBest( boonPickups );
		CheckBest( push );
		CheckBest( pull );
		CheckBest( healed );
		CheckBest( energizedAlly );
		CheckBest( gotEnergizedByAlly );
		CheckBest( energizeEfficiency );
		CheckBest( energizedEnemy );
		CheckBest( absorbed );
		CheckBest( protDamageAvoided );
		CheckBest( protTimeUsed );
		CheckBest( rageTimeUsed );
		CheckBest( drain );
		CheckBest( gotDrained );
		CheckBest( grips );
		CheckBest( gotGripped );
		CheckBest( darkPercent );
		for ( i = CTFREGION_FLAGSTAND; i <= CTFREGION_ENEMYFLAGSTAND; i++ )
		{
			CheckBest( regionTime[i] );
			CheckBest( regionPercent[i] );
		}
		CheckBest( averageGetHealth );
	}
	else if ( type == STATS_TABLE_DAMAGE )
	{
		beIter_t iter;

		BE_ListIterate( &player->damageGivenList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			const damageCounter_t *given = (const damageCounter_t *)BE_IterNext( &iter );
			damageCounter_t *found = NULL;
			beIter_t iter2;

			if ( given->totalAmount <= 0 )
				continue;
			BE_ListIterate( &best->damageGivenList, &iter2 );
			while ( BE_IterHasNext( &iter2 ) )
			{
				damageCounter_t *thisBest = (damageCounter_t *)BE_IterNext( &iter2 );
				if ( thisBest->otherPlayerSessionId == given->otherPlayerSessionId )
				{
					found = thisBest;
					break;
				}
			}
			if ( found )
			{
				if ( given->totalAmount > found->totalAmount )
					found->totalAmount = given->totalAmount;
			}
			else
			{
				found = (damageCounter_t *)BE_ListAdd( &best->damageGivenList, sizeof( damageCounter_t ) );
				if ( found )
				{
					found->otherPlayerSessionId = given->otherPlayerSessionId;
					found->otherPlayerIsBot = given->otherPlayerIsBot;
					found->totalAmount = given->totalAmount;
				}
			}
		}
	}
	else if ( type == STATS_TABLE_WEAPON_GIVEN || type == STATS_TABLE_WEAPON_TAKEN )
	{
		int all = 0;

		for ( i = MODC_FIRST; i < MODC_ALL_TYPES_COMBINED; i++ )
		{
			int dmg = ( type == STATS_TABLE_WEAPON_GIVEN ) ? GetTotalDamageGivenStatOfType( weaponStatsPtr, player, (meansOfDeathCategory_t)i ) :
				GetTotalDamageTakenStatOfType( player, weaponStatsPtr, (meansOfDeathCategory_t)i );
			all += dmg;
			if ( dmg > best->damageOfType[i] )
				best->damageOfType[i] = dmg;
		}
		if ( all > best->damageOfType[MODC_ALL_TYPES_COMBINED] )
			best->damageOfType[MODC_ALL_TYPES_COMBINED] = all;
	}
	else if ( type == STATS_TABLE_ACCURACY )
	{
		CheckBest( accuracy );
		for ( i = ACC_FIRST; i < ACC_MAX; i++ )
			CheckBest( accuracyOfType[i] );
	}
}

#define AddStatToTotal( field )	do { total->field += player->field; } while ( 0 )

// AddStatsToTotal: adds a player's (or block's) stats into a combined row or a team total
static void AddStatsToTotal( beStats_t *player, beStats_t *total, statsTableType_t type, beStats_t *weaponStatsPtr )
{
	int i;

	if ( type == STATS_TABLE_GENERAL )
	{
		AddStatToTotal( captures );
		AddStatToTotal( assists );
		AddStatToTotal( defends );
		AddStatToTotal( accuracy_shots );
		AddStatToTotal( accuracy_hits );
		AddStatToTotal( airs );
		AddStatToTotal( pits );
		AddStatToTotal( pitted );
		AddStatToTotal( fcKills );
		AddStatToTotal( fcKillsResultingInRets );
		AddStatToTotal( flagCarrierDamageDealtTotal );
		AddStatToTotal( flagCarrierDamageTakenTotal );
		AddStatToTotal( clearDamageDealtTotal );
		AddStatToTotal( clearDamageTakenTotal );
		AddStatToTotal( otherDamageDealtTotal );
		AddStatToTotal( otherDamageTakenTotal );
		AddStatToTotal( teamKills );
		AddStatToTotal( takes );
		AddStatToTotal( rets );
		AddStatToTotal( selfkills );
		AddStatToTotal( healthPickedUp );
		AddStatToTotal( armorPickedUp );
		AddStatToTotal( totalFlagHold );
		AddStatToTotal( numFlagHolds );
		if ( player->longestFlagHold > total->longestFlagHold )
			total->longestFlagHold = player->longestFlagHold;
		AddStatToTotal( saves );
		AddStatToTotal( damageDealtTotal );
		AddStatToTotal( damageTakenTotal );
		if ( player->topSpeed > total->topSpeed )
			total->topSpeed = player->topSpeed;
		AddStatToTotal( displacement );
		AddStatToTotal( displacementMs );
	}
	else if ( type == STATS_TABLE_FORCE )
	{
		AddStatToTotal( boonPickups );
		AddStatToTotal( push );
		AddStatToTotal( pull );
		AddStatToTotal( healed );
		AddStatToTotal( energizedAlly );
		AddStatToTotal( gotEnergizedByAlly );
		AddStatToTotal( numEnergizes );
		AddStatToTotal( normalizedEnergizeAmounts );
		AddStatToTotal( energizedEnemy );
		AddStatToTotal( absorbed );
		AddStatToTotal( protDamageAvoided );
		AddStatToTotal( protTimeUsed );
		AddStatToTotal( rageTimeUsed );
		AddStatToTotal( drain );
		AddStatToTotal( gotDrained );
		AddStatToTotal( grips );
		AddStatToTotal( gotGripped );
		AddStatToTotal( forceSamples );
		AddStatToTotal( darkForceSamples );
		for ( i = CTFREGION_FLAGSTAND; i <= CTFREGION_ENEMYFLAGSTAND; i++ )
			AddStatToTotal( regionTime[i] );
		AddStatToTotal( numGets );
		AddStatToTotal( getTotalHealth );
	}
	else if ( type == STATS_TABLE_DAMAGE )
	{
		beIter_t iter;

		BE_ListIterate( &player->damageGivenList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			const damageCounter_t *given = (const damageCounter_t *)BE_IterNext( &iter );
			damageCounter_t *found = NULL;
			beIter_t iter2;

			if ( given->totalAmount <= 0 )
				continue;
			BE_ListIterate( &total->damageGivenList, &iter2 );
			while ( BE_IterHasNext( &iter2 ) )
			{
				damageCounter_t *thisTotal = (damageCounter_t *)BE_IterNext( &iter2 );
				if ( thisTotal->otherPlayerSessionId == given->otherPlayerSessionId )
				{
					found = thisTotal;
					break;
				}
			}
			if ( !found )
			{
				found = (damageCounter_t *)BE_ListAdd( &total->damageGivenList, sizeof( damageCounter_t ) );
				if ( !found )
					continue;
				found->otherPlayerSessionId = given->otherPlayerSessionId;
				found->otherPlayerIsBot = given->otherPlayerIsBot;
			}
			found->totalAmount += given->totalAmount;
		}
	}
	else if ( ( type == STATS_TABLE_WEAPON_GIVEN || type == STATS_TABLE_WEAPON_TAKEN ) && total->isTotal )
	{ // only the team total row reads damageOfType
		for ( i = MODC_FIRST; i < MODC_ALL_TYPES_COMBINED; i++ )
			total->damageOfType[i] += ( type == STATS_TABLE_WEAPON_GIVEN ) ? GetTotalDamageGivenStatOfType( weaponStatsPtr, player, (meansOfDeathCategory_t)i ) :
				GetTotalDamageTakenStatOfType( player, weaponStatsPtr, (meansOfDeathCategory_t)i );
	}
	else if ( type == STATS_TABLE_ACCURACY )
	{
		AddStatToTotal( accuracy_shots );
		AddStatToTotal( accuracy_hits );
		for ( i = ACC_FIRST; i < ACC_MAX; i++ )
		{
			AddStatToTotal( accuracy_shotsOfType[i] );
			AddStatToTotal( accuracy_hitsOfType[i] );
		}
	}
	SetDerivedStats( total );
}

/*
================================================================================
Output: chunked prints (upstream PrintIngame)
================================================================================
*/

#define BE_PRINT_CHUNK	(1000)
#define BE_PRINT_MAX_PER_FRAME	(64)	// reliable commands to one client in one frame; the engine drops a client past 128

static int		printFrame[MAX_CLIENTS], printCount[MAX_CLIENTS];

static void BE_StatsSendChunks( int clientNum, const char *text, qboolean toConsole )
{
	char chunk[BE_PRINT_CHUNK + 1], cmd[MAX_STRING_CHARS];
	char activeColor = '7';
	const char *p = text;

	while ( *p )
	{
		int len = (int)strlen( p ), i;

		if ( len > BE_PRINT_CHUNK )
		{ // end the chunk at a line break when there is one
			int cut = BE_PRINT_CHUNK;
			for ( i = BE_PRINT_CHUNK - 1; i > BE_PRINT_CHUNK / 2; i-- )
			{
				if ( p[i] == '\n' )
				{
					cut = i + 1;
					break;
				}
			}
			if ( cut == BE_PRINT_CHUNK && p[cut - 1] == Q_COLOR_ESCAPE )
				cut--; // never split a colour code
			len = cut;
		}
		memcpy( chunk, p, len );
		chunk[len] = '\0';

		if ( toConsole )
		{
			trap->Print( "%s", chunk );
		}
		else
		{
			if ( printFrame[clientNum] != level.time )
			{
				printFrame[clientNum] = level.time;
				printCount[clientNum] = 0;
			}
			if ( printCount[clientNum] >= BE_PRINT_MAX_PER_FRAME )
				return; // the rest would risk an overflow; ask for a smaller table
			if ( ++printCount[clientNum] == BE_PRINT_MAX_PER_FRAME )
				Q_strncpyz( chunk, "^7(stats output cut short; ask for one table or one player)\n", sizeof( chunk ) );
			Com_sprintf( cmd, sizeof( cmd ), "print \"^%c%s\"", activeColor, chunk );
			trap->SendServerCommand( clientNum, cmd );
		}
		for ( i = 0; i < len - 1; i++ )
		{ // the colour that carries into the next chunk
			if ( Q_IsColorString( chunk + i ) )
				activeColor = chunk[i + 1];
		}
		p += len;
	}
}

// PrintIngame: to one client, to every human (BE_OUT_ALL, plus the console), or the console
static void StatsOut( int id, const char *text )
{
	if ( !VALIDSTRING( text ) )
		return;

	if ( id >= 0 && id < MAX_CLIENTS )
	{
		if ( g_entities[id].inuse && !( g_entities[id].r.svFlags & SVF_BOT ) &&
			level.clients[id].pers.connected == CON_CONNECTED )
			BE_StatsSendChunks( id, text, qfalse );
		return;
	}
	if ( id == BE_OUT_ALL )
	{
		int i;
		for ( i = 0; i < level.maxclients; i++ )
		{
			if ( g_entities[i].inuse && !( g_entities[i].r.svFlags & SVF_BOT ) && level.clients[i].pers.connected == CON_CONNECTED )
				BE_StatsSendChunks( i, text, qfalse );
		}
	}
	if ( id == BE_OUT_CONSOLE )
	{
		BE_StatsSendChunks( -1, text, qtrue );
	}
	else if ( id == BE_OUT_ALL || id == BE_OUT_LOG )
	{
		size_t len = strlen( text ) + 1;
		char *clean = (char *)malloc( len );
		if ( clean )
		{
			memcpy( clean, text, len );
			Q_StripColor( clean );
			BE_StatsSendChunks( -1, clean, qtrue );
			free( clean );
		}
	}
}

static void QDECL StatsOutf( int id, const char *fmt, ... )
{
	va_list argptr;
	char text[4096];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );
	StatsOut( id, text );
}

/*
================================================================================
Tables (upstream PrintTeamStats)
================================================================================
*/

#define STATSTABLE_NAME			"^5Name"
#define STATSTABLE_NAME_LENGTH	(4)
#define STATSTABLE_ALIAS		"^5Alias"
#define STATSTABLE_ALIAS_LENGTH	(5)
#define TEMP_STATS_BUFFER_SIZE	(32768)

typedef struct {
	beNode_t	node;
	int			sessionId;
	qboolean	isBot;
	beStats_t	*stats;
	team_t		team;
} gotPlayer_t;

typedef struct {
	beNode_t	node;
	char		str[MAX_NETNAME + 16];
} stringListEntry_t;

static qboolean PlayerMatches( void *element, void *userData )
{
	const gotPlayer_t *existing = (const gotPlayer_t *)element;
	const beStats_t *thisGuy = (const beStats_t *)userData;

	return ( thisGuy->sessionId == existing->sessionId && thisGuy->isBot == existing->isBot && thisGuy->lastTeam == existing->team ) ? qtrue : qfalse;
}

static void ClearBestStats( void )
{
	int i;

	for ( i = 0; i < TEAM_NUM_TEAMS; i++ )
		BE_StatsFreeLists( &bestStats[i] );
	memset( bestStats, 0, sizeof( bestStats ) );
}

typedef struct {
	beTable_t		*t;
	statsTableType_t	type;
	beStats_t		*weaponStatsPtr;
	beList_t		*gotPlayersList;
	beList_t		*combinedStatsList;
	team_t			winningTeam, losingTeam;
	int				numWinningTeam, numLosingTeam;
	qboolean		didHorizontalRule;
	beStats_t		*winningTeamTotal, *losingTeamTotal;
	beStats_t		*lastWinningTeamPlayer;
} teamStatsBuild_t;

// one table row: found plus every other block of the same player on the same team
static qboolean AddTeamStatsRow( teamStatsBuild_t *b, beStats_t *found, team_t team )
{
	gotPlayer_t *add;
	beStats_t *combined;
	ctfPosition_t pos;
	beIter_t iter;

	if ( BE_ListFind( b->gotPlayersList, PlayerMatches, found ) )
		return qfalse;
	pos = found->finalPosition ? found->finalPosition : DetermineCTFPosition( found );

	add = (gotPlayer_t *)BE_ListAdd( b->gotPlayersList, sizeof( gotPlayer_t ) );
	if ( !add )
		return qfalse;
	add->stats = found;
	add->sessionId = found->sessionId;
	add->isBot = found->isBot;
	add->team = found->lastTeam;

	if ( team == b->winningTeam )
	{
		b->lastWinningTeamPlayer = found;
	}
	else if ( b->numWinningTeam && !b->didHorizontalRule )
	{ // there were winners and this is the first loser
		if ( b->numWinningTeam > 1 )
			BE_TableDefineRow( b->t, b->winningTeamTotal );
		BE_TableAddHorizontalRule( b->t, b->losingTeam == TEAM_BLUE ? 4 : 1 );
		b->didHorizontalRule = qtrue;
	}

	combined = (beStats_t *)BE_ListAdd( b->combinedStatsList, sizeof( beStats_t ) );
	if ( !combined )
		return qtrue;
	AddStatsToTotal( found, combined, b->type, b->weaponStatsPtr );
	Q_strncpyz( combined->name, found->name, sizeof( combined->name ) );
	combined->sessionId = found->sessionId;
	combined->isBot = found->isBot;
	combined->clientNum = found->clientNum;
	combined->lastTeam = found->lastTeam;
	combined->finalPosition = pos;
	combined->msIngame += found->msIngame;
	combined->confirmedPositionBits |= found->confirmedPositionBits;

	BE_ListIterate( &savedStatsList, &iter );
	while ( BE_IterHasNext( &iter ) )
	{
		beStats_t *match = (beStats_t *)BE_IterNext( &iter );
		if ( StatsValid( match ) && match->isBot == found->isBot && match->sessionId == found->sessionId && match->lastTeam == found->lastTeam && match != found )
		{
			AddStatsToTotal( match, combined, b->type, b->weaponStatsPtr );
			combined->msIngame += match->msIngame;
			combined->confirmedPositionBits |= match->confirmedPositionBits;
		}
	}

	CheckBestStats( combined, b->type, b->weaponStatsPtr );
	AddStatsToTotal( combined, team == b->winningTeam ? b->winningTeamTotal : b->losingTeamTotal, b->type, b->weaponStatsPtr );
	BE_TableDefineRow( b->t, combined );

	if ( team == b->winningTeam )
		++b->numWinningTeam;
	else
		++b->numLosingTeam;
	return qtrue;
}

// PrintTeamStats: one table, rows by team (winners first), connected players
// in scoreboard order, then leavers and team changers, then old blocks
static void PrintTeamStats( int id, statsTableType_t type, beStats_t *weaponStatsPtr )
{
	teamStatsBuild_t b;
	beList_t gotPlayersList = { 0 }, combinedStatsList = { 0 };
	beStats_t winningTeamTotalStats, losingTeamTotalStats;
	int longestPrintLenName = STATSTABLE_NAME_LENGTH, longestPrintLenAlias = STATSTABLE_ALIAS_LENGTH, longestPrintLenPos = 0;
	int headerColor, i, j;
	char *temp;
	beIter_t iter;

	memset( &b, 0, sizeof( b ) );
	b.t = BE_TableInit( qfalse );
	if ( !b.t )
		return;
	ClearBestStats();
	memset( &winningTeamTotalStats, 0, sizeof( winningTeamTotalStats ) );
	memset( &losingTeamTotalStats, 0, sizeof( losingTeamTotalStats ) );
	b.type = type;
	b.weaponStatsPtr = weaponStatsPtr;
	b.gotPlayersList = &gotPlayersList;
	b.combinedStatsList = &combinedStatsList;
	b.winningTeam = level.teamScores[TEAM_BLUE] > level.teamScores[TEAM_RED] ? TEAM_BLUE : TEAM_RED; // red if tied, as the scoreboard
	b.losingTeam = (team_t)OtherTeam( b.winningTeam );
	winningTeamTotalStats.isTotal = losingTeamTotalStats.isTotal = qtrue;
	winningTeamTotalStats.lastTeam = b.winningTeam;
	losingTeamTotalStats.lastTeam = b.losingTeam;
	b.winningTeamTotal = &winningTeamTotalStats;
	b.losingTeamTotal = &losingTeamTotalStats;

	for ( j = 0; j < 2; j++ )
	{
		team_t team = !j ? b.winningTeam : b.losingTeam;

		// connected clients first, in scoreboard order...
		for ( i = 0; i < level.numConnectedClients; i++ )
		{
			int findClientNum = level.sortedClients[i];
			BE_ListIterate( &statsList, &iter );
			while ( BE_IterHasNext( &iter ) )
			{
				beStats_t *found = (beStats_t *)BE_IterNext( &iter );
				if ( !StatsValid( found ) || found->clientNum != findClientNum || found->lastTeam != team )
					continue;
				if ( AddTeamStatsRow( &b, found, team ) )
					break;
			}
		}
		// ...then everybody else (leavers, people who changed teams or went spec)...
		BE_ListIterate( &statsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *found = (beStats_t *)BE_IterNext( &iter );
			if ( StatsValid( found ) && found->lastTeam == team )
				AddTeamStatsRow( &b, found, team );
		}
		// ...and finally the old blocks
		BE_ListIterate( &savedStatsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *found = (beStats_t *)BE_IterNext( &iter );
			if ( StatsValid( found ) && found->lastTeam == team )
				AddTeamStatsRow( &b, found, team );
		}
	}

	if ( b.numLosingTeam > 1 )
		BE_TableDefineRow( b.t, &losingTeamTotalStats );

	if ( !b.numWinningTeam && !b.numLosingTeam )
		goto cleanup;

	if ( type == STATS_TABLE_GENERAL )
	{
		BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Time", Cell_Time, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Cap", Cell_Captures, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Ass", Cell_Assists, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Def", Cell_Defends, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Acc", Cell_Accuracy, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Air", Cell_Airs, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5TK", Cell_TeamKills, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Take", Cell_Takes, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Pit", Cell_Pits, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Dth", Cell_Pitted, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Dmg", Cell_DamageDealt, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Fc", Cell_FlagCarrierDamageDealt, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Clr", Cell_ClearDamageDealt, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Othr", Cell_OtherDamageDealt, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Tkn", Cell_DamageTaken, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Fc", Cell_FlagCarrierDamageTaken, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Clr", Cell_ClearDamageTaken, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Othr", Cell_OtherDamageTaken, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6FcKil", Cell_FcKills, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Eff", Cell_FlagCarrierKillEfficiency, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Ret", Cell_Rets, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5SK", Cell_Selfkills, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^3Hold", Cell_TotalHold, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^3Max", Cell_MaxHold, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Spd", Cell_AverageSpeed, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Top", Cell_TopSpeed, NULL, qfalse, -1, 32 );
	}
	else if ( type == STATS_TABLE_FORCE )
	{
		qboolean boonExists;
		int region;

		ExistingWeaponSpawns( &boonExists );
		BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, -1, 32 );
		if ( boonExists ) // only if boon exists on this map
			BE_TableDefineColumn( b.t, "^5Boon", Cell_BoonPickups, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Push", Cell_Push, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Pull", Cell_Pull, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Heal", Cell_Healed, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6TE", Cell_EnergizedAlly, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Eff", Cell_EnergizeEfficiency, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^6Rcvd", Cell_GotEnergizedByAlly, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5EnemyNrg", Cell_EnergizedEnemy, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Abs", Cell_Absorbed, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Prot", Cell_ProtDamage, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^2Time", Cell_ProtTime, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Rage", Cell_RageTime, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Drn", Cell_Drain, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Drnd", Cell_GotDrained, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Gr", Cell_Grips, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Gd", Cell_GotGripped, NULL, qfalse, -1, 32 );
		BE_TableDefineColumn( b.t, "^1Drk", Cell_DarkPercent, NULL, qfalse, -1, 32 );
		region = CTFREGION_FLAGSTAND;
		BE_TableDefineColumn( b.t, "^5Fs", Cell_RegionPercent, &region, qfalse, -1, 32 );
		++region;
		BE_TableDefineColumn( b.t, "^5Bas", Cell_RegionPercent, &region, qfalse, -1, 32 );
		++region;
		BE_TableDefineColumn( b.t, "^5Mid", Cell_RegionPercent, &region, qfalse, -1, 32 );
		++region;
		BE_TableDefineColumn( b.t, "^5EBa", Cell_RegionPercent, &region, qfalse, -1, 32 );
		++region;
		BE_TableDefineColumn( b.t, "^5EFs", Cell_RegionPercent, &region, qfalse, -1, 32 );
	}
	else if ( type == STATS_TABLE_DAMAGE )
	{
		beList_t stringList = { 0 };
		int firstDividerColor;
		qboolean didFirstColumns = qfalse;

		if ( b.numWinningTeam )
			firstDividerColor = ( b.winningTeam == TEAM_RED ) ? 1 : 4;
		else
			firstDividerColor = ( b.losingTeam == TEAM_RED ) ? 1 : 4;

		// pos is shown if anyone has one; work that out before the first columns
		BE_ListIterate( &gotPlayersList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			gotPlayer_t *player = (gotPlayer_t *)BE_IterNext( &iter );
			int len = Q_PrintStrlen( player->stats->name );
			if ( DetermineCTFPosition( player->stats ) )
				longestPrintLenPos = 3; // "Pos" spacing
			if ( len > longestPrintLenName )
				longestPrintLenName = len;
			if ( player->stats->isBot && longestPrintLenAlias < 3 )
				longestPrintLenAlias = 3;
		}

		BE_ListIterate( &gotPlayersList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			gotPlayer_t *player = (gotPlayer_t *)BE_IterNext( &iter );
			stringListEntry_t *addStr;
			char clean[MAX_NETNAME];
			ctfPosition_t pos = DetermineCTFPosition( player->stats );
			int secondDividerColor = -1;

			Q_strncpyz( clean, player->stats->name, sizeof( clean ) );
			Q_StripColor( clean );
			addStr = (stringListEntry_t *)BE_ListAdd( &stringList, sizeof( stringListEntry_t ) );
			if ( !addStr )
				continue;
			if ( pos )
				Com_sprintf( addStr->str, sizeof( addStr->str ), "^5%s (%s)", clean, PosAbbreviation( pos ) );
			else
				Com_sprintf( addStr->str, sizeof( addStr->str ), "^5%s", clean );

			if ( b.numWinningTeam && b.numLosingTeam && player->stats == b.lastWinningTeamPlayer )
				secondDividerColor = ( b.winningTeam == TEAM_RED ) ? 4 : 1;

			if ( !didFirstColumns )
			{ // the divider goes after pos if there is one, else after alias
				BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
				BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, longestPrintLenPos ? -1 : firstDividerColor, 32 );
				BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, longestPrintLenPos ? firstDividerColor : -1, 32 );
				didFirstColumns = qtrue;
			}
			BE_TableDefineColumn( b.t, addStr->str, Cell_Damage, player->stats, qfalse, secondDividerColor, 32 );
		}
		if ( !didFirstColumns )
		{
			BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
			BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, longestPrintLenPos ? -1 : firstDividerColor, 32 );
			BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, longestPrintLenPos ? firstDividerColor : -1, 32 );
		}
		BE_ListClear( &stringList );
	}
	else if ( type == STATS_TABLE_WEAPON_GIVEN || type == STATS_TABLE_WEAPON_TAKEN )
	{
		meansOfDeathCategoryContext_t context;
		meansOfDeathCategory_t modc;

		BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, -1, 32 );
		for ( modc = MODC_FIRST; modc < MODC_MAX; modc++ )
		{
			context.tablePlayerStats = weaponStatsPtr;
			context.damageTaken = ( type == STATS_TABLE_WEAPON_TAKEN ) ? qtrue : qfalse;
			context.modc = modc;
			BE_TableDefineColumn( b.t, NameForMeansOfDeathCategory( modc ), Cell_WeaponDamage, &context, qfalse, -1, 32 );
		}
	}
	else if ( type == STATS_TABLE_ACCURACY )
	{
		accuracyCategory_t weap;

		existingWeaponSpawns = ExistingWeaponSpawns( NULL );
		BE_TableDefineColumn( b.t, STATSTABLE_NAME, Cell_Name, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, STATSTABLE_ALIAS, Cell_Alias, NULL, qtrue, -1, 32 );
		BE_TableDefineColumn( b.t, "^5Pos", Cell_Position, NULL, qtrue, -1, 32 );
		for ( weap = ACC_FIRST; weap < ACC_MAX; weap++ )
			BE_TableDefineColumn( b.t, NameForAccuracyCategory( weap ), Cell_WeaponAccuracy, &weap, qfalse, -1, 32 );
	}

	headerColor = b.numWinningTeam ? ( b.winningTeam == TEAM_BLUE ? 4 : 1 ) : ( b.losingTeam == TEAM_BLUE ? 4 : 1 );
	temp = (char *)calloc( 1, TEMP_STATS_BUFFER_SIZE );
	if ( temp )
	{
		if ( type == STATS_TABLE_DAMAGE )
		{
			int spaces = ( longestPrintLenName + 1 ) + ( longestPrintLenAlias + 1 ) + 2 + ( longestPrintLenPos ? longestPrintLenPos + 1 : 0 );
			int len;
			for ( i = 0; i < spaces && i < 256; i++ )
				Q_strcat( temp, TEMP_STATS_BUFFER_SIZE, " " );
			Q_strcat( temp, TEMP_STATS_BUFFER_SIZE, "^5Damage dealt to\n" );
			len = strlen( temp );
			BE_TableWrite( b.t, temp + len, TEMP_STATS_BUFFER_SIZE - len, qtrue, headerColor );
		}
		else if ( type == STATS_TABLE_WEAPON_GIVEN || type == STATS_TABLE_WEAPON_TAKEN )
		{
			ctfPosition_t pos = DetermineCTFPosition( weaponStatsPtr );
			const char *posStr = pos ? GetPositionStringForStats( weaponStatsPtr ) : NULL;
			int len;
			Com_sprintf( temp, TEMP_STATS_BUFFER_SIZE, "%s^7 by %s^7 (%s%s%s)^7:\n",
				type == STATS_TABLE_WEAPON_GIVEN ? "^2Damage DEALT" : "^8Damage TAKEN",
				weaponStatsPtr->name,
				weaponStatsPtr->lastTeam == TEAM_RED ? "Red" : "Blue",
				posStr ? " " : "", posStr ? posStr : "" );
			len = strlen( temp );
			BE_TableWrite( b.t, temp + len, TEMP_STATS_BUFFER_SIZE - len, qtrue, headerColor );
		}
		else
		{
			BE_TableWrite( b.t, temp, TEMP_STATS_BUFFER_SIZE, qtrue, headerColor );
		}
		StatsOut( id, temp );
		free( temp );
	}

cleanup:
	BE_TableDestroy( b.t );
	BE_ListClear( &gotPlayersList );
	BE_StatsClearStatsList( &combinedStatsList );
	BE_StatsFreeLists( &winningTeamTotalStats );
	BE_StatsFreeLists( &losingTeamTotalStats );
	ClearBestStats();
}

/*
================================================================================
Help (upstream statsHelp_t helps[])
================================================================================
*/

typedef struct {
	statsTableType_t	type;
	const char			*shortName;
	const char			*longName;
	const char			*description;
} statsHelp_t;

// any new stat must not conflict with the help subcommands that print whole categories
static const statsHelp_t helps[] = {
	{ STATS_TABLE_GENERAL, "^5Pos", "Position", "CTF position, if 4v4" },
	{ STATS_TABLE_GENERAL, "^5Time", "Time", "Time in game" },
	{ STATS_TABLE_GENERAL, "^5Cap", "Captures", "Times you captured the flag" },
	{ STATS_TABLE_GENERAL, "^5Ass", "Assists", "Flag carrier kills, or flag returns, within ten seconds prior to your team capturing the flag" },
	{ STATS_TABLE_GENERAL, "^5Def", "Defends", "Kills on enemies near your flag, near your flag carrier, or who hurt your flag carrier within the past eight seconds" },
	{ STATS_TABLE_GENERAL, "^5Acc", "Accuracy", "Total accuracy percentage, only including disrupts, snipes, repeater blobs, golan balls, rockets, alt thermals, and both concs" },
	{ STATS_TABLE_GENERAL, "^5Air", "Air shots", "Hits on midair enemies, only including explosive projectiles, bowcaster, and charged pistol; does not have to kill" },
	{ STATS_TABLE_GENERAL, "^5TK", "Teamkills", "Bad kills on teammates; does not include killing your flag carrier if an ally takes the flag afterward" },
	{ STATS_TABLE_GENERAL, "^5Take", "Takes", "Times you killed your flag carrier and then took the flag" },
	{ STATS_TABLE_GENERAL, "^6Pit", "Pit kills", "Kills on enemies using pits" },
	{ STATS_TABLE_GENERAL, "^6Dth", "Pit deaths", "Times you got killed by enemies using pits" },
	{ STATS_TABLE_GENERAL, "^2Dmg", "Damage", "Total damage dealt to enemies" },
	{ STATS_TABLE_GENERAL, "^2Fc", "Flag carrier damage", "Damage dealt to enemy flag carriers" },
	{ STATS_TABLE_GENERAL, "^2Clr", "Clear damage", "Damage dealt to non-flag-carrying enemies inside your base" },
	{ STATS_TABLE_GENERAL, "^2Othr", "Other damage", "Damage dealt to enemies outside your base who are not carrying the flag" },
	{ STATS_TABLE_GENERAL, "^1Tkn", "Damage taken", "Total damage taken from enemies" },
	{ STATS_TABLE_GENERAL, "^1Fc", "Flag carrier damage taken", "Damage taken from enemies while carrying the flag" },
	{ STATS_TABLE_GENERAL, "^1Clr", "Clear damage taken", "Damage taken from enemies while you are in their base and not carrying the flag" },
	{ STATS_TABLE_GENERAL, "^1Othr", "Other damage taken", "Damage taken from enemies while you are outside their base and not carrying the flag" },
	{ STATS_TABLE_GENERAL, "^6FcKil", "Flag carrier kills", "Kills on enemy flag carriers" },
	{ STATS_TABLE_GENERAL, "^6FcKil Eff", "Flag carrier kill efficiency", "Percentage of your kills on flag carriers that resulted in the flag being returned by you or a teammate" },
	{ STATS_TABLE_GENERAL, "^5Ret", "Returns", "Times you returned your flag" },
	{ STATS_TABLE_GENERAL, "^5SK", "Selfkills", "Suicides, including falling into pits on your own" },
	{ STATS_TABLE_GENERAL, "^3Hold", "Total hold", "Total time you carried the flag" },
	{ STATS_TABLE_GENERAL, "^3MaxHold", "Maximum hold", "The duration of your longest flag hold" },
	{ STATS_TABLE_GENERAL, "^6Spd", "Average speed", "Average movement speed, in units per second" },
	{ STATS_TABLE_GENERAL, "^6Top", "Top speed", "The highest movement speed you reached, in units per second" },
	{ STATS_TABLE_GENERAL, "^5Boon", "Boons", "Boon pickups, if boon is enabled" },
	{ STATS_TABLE_GENERAL, "^5Push", "Pushes", "Force pushes used" },
	{ STATS_TABLE_GENERAL, "^5Pull", "Pulls", "Force pulls used" },
	{ STATS_TABLE_GENERAL, "^5Heal", "Team heal", "Amount of allies' health you replenished with team heal" },
	{ STATS_TABLE_GENERAL, "^6TE", "Team energize", "Amount of allies' force power you replenished with team energize" },
	{ STATS_TABLE_GENERAL, "^6TE Eff", "Team energize efficiency", "Efficiency of your team energize usage (highest amount anyone was energized / maximum amount anyone could have been energized)" },
	{ STATS_TABLE_GENERAL, "^6Rcvd", "Team energize received", "Amount of force power you had replenished by allies with team energize" },
	{ STATS_TABLE_GENERAL, "^5EnemyNrg", "Enemy energize", "Force power given to enemies using absorb" },
	{ STATS_TABLE_GENERAL, "^5Abs", "Absorb", "Force power absorbed from enemies" },
	{ STATS_TABLE_GENERAL, "^2Prot", "Protect damage", "Damage avoided using protect" },
	{ STATS_TABLE_GENERAL, "^2Time", "Protect time", "Total duration you had protect activated" },
	{ STATS_TABLE_GENERAL, "^5Rage", "Rage time", "Total duration you had rage activated" },
	{ STATS_TABLE_GENERAL, "^1Drn", "Drain", "Force power gained by draining enemies" },
	{ STATS_TABLE_GENERAL, "^1Drnd", "Drained", "Force power given to enemies by drain" },
	{ STATS_TABLE_GENERAL, "^1Gr", "Grips", "Number of times you gripped enemies" },
	{ STATS_TABLE_GENERAL, "^1Gd", "Gripped", "Number of times you were gripped by enemies" },
	{ STATS_TABLE_GENERAL, "^1Drk", "Dark force use", "Percentage of the match using dark side force" },
	{ STATS_TABLE_GENERAL, "^5Fs", "Allied flagstand time", "Percentage of the match spent in the 20 percent of the map closest to your flagstand" },
	{ STATS_TABLE_GENERAL, "^5Bas", "Allied base time", "Percentage of the match spent in the 20 percent of the map between your flagstand and mid" },
	{ STATS_TABLE_GENERAL, "^5Mid", "Mid time", "Percentage of the match spent in the middle 20 percent of the map" },
	{ STATS_TABLE_GENERAL, "^5EBa", "Enemy base time", "Percentage of the match spent in the 20 percent of the map between the enemy flagstand and mid" },
	{ STATS_TABLE_GENERAL, "^5EFs", "Enemy flagstand time", "Percentage of the match spent in the 20 percent of the map closest to the enemy flagstand" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Mel", "Melee damage", "Damage dealt to enemies with melee" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Sab", "Lightsaber damage", "Damage dealt to enemies with lightsaber" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Pis", "Pistol damage", "Damage dealt to enemies with pistol" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Bla", "Blaster damage", "Damage dealt to enemies with blaster" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Dis", "Disruptor damage", "Damage dealt to enemies with disruptor" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Bow", "Bowcaster damage", "Damage dealt to enemies with bowcaster" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Rep", "Repeater damage", "Damage dealt to enemies with repeater" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Dmp", "Demp damage", "Damage dealt to enemies with demp" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Gol", "Golan damage", "Damage dealt to enemies with golan" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Roc", "Rocket damage", "Damage dealt to enemies with rockets" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Con", "Concussion rifle damage", "Damage dealt to enemies with concussion rifle" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5The", "Thermal detonator damage", "Damage dealt to enemies with thermal detonators" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Min", "Mine damage", "Damage dealt to enemies with mines" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Dpk", "Detpack damage", "Damage dealt to enemies with detpacks" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5For", "Force damage", "Damage dealt to enemies with force powers" },
	{ STATS_TABLE_WEAPON_GIVEN, "^5Total", "Total damage", "Total damage dealt to enemies" },
	{ STATS_TABLE_ACCURACY, "^5Pos", "Position", "CTF position, if 4v4" },
	{ STATS_TABLE_ACCURACY, "^5Acc", "Accuracy", "Total accuracy percentage, only including disrupts, snipes, repeater blobs, golan balls, rockets, alt thermals, and both concs" },
	{ STATS_TABLE_ACCURACY, "^5Pis", "Pistol altfire accuracy", "Accuracy percentage of pistol altfire (must be charged at least 400ms)" },
	{ STATS_TABLE_ACCURACY, "^5Dis", "Disruptor primary accuracy", "Accuracy percentage of disruptor primary" },
	{ STATS_TABLE_ACCURACY, "^5Snp", "Disruptor snipe accuracy", "Accuracy percentage of disruptor snipes" },
	{ STATS_TABLE_ACCURACY, "^5Rep", "Repeater blob accuracy", "Accuracy percentage of repeater blobs" },
	{ STATS_TABLE_ACCURACY, "^5Gol", "Golan ball accuracy", "Accuracy percentage of golan balls" },
	{ STATS_TABLE_ACCURACY, "^5Roc", "Rocket accuracy", "Accuracy percentage of rockets" },
	{ STATS_TABLE_ACCURACY, "^5Con", "Concussion rifle primary accuracy", "Accuracy percentage of concussion rifle primary" },
	{ STATS_TABLE_ACCURACY, "^5Alt", "Concussion rifle altfire accuracy", "Accuracy percentage of concussion rifle altfire" },
	{ STATS_TABLE_ACCURACY, "^5The", "Thermal detonator altfire accuracy", "Accuracy percentage of thermal detonator altfire" },
};

static const char *HelpCell_Category( void *rowContext, void *columnContext )
{
	switch ( ( (const statsHelp_t *)rowContext )->type )
	{
	case STATS_TABLE_GENERAL: return "General";
	case STATS_TABLE_DAMAGE: return "Damage";
	case STATS_TABLE_WEAPON_GIVEN: return "Weapon";
	case STATS_TABLE_ACCURACY: return "Accuracy";
	default: return NULL;
	}
}

static const char *HelpCell_ShortName( void *rowContext, void *columnContext )
{
	return ( (const statsHelp_t *)rowContext )->shortName;
}

static const char *HelpCell_LongName( void *rowContext, void *columnContext )
{
	return ( (const statsHelp_t *)rowContext )->longName;
}

static const char *HelpCell_Description( void *rowContext, void *columnContext )
{
	return ( (const statsHelp_t *)rowContext )->description;
}

static void PrintHelpTable( beTable_t *t, int id, qboolean withType )
{
	char *buf = (char *)calloc( 1, 16384 );

	if ( withType )
		BE_TableDefineColumn( t, "Type", HelpCell_Category, NULL, qtrue, -1, 32 );
	BE_TableDefineColumn( t, withType ? "Short name" : "Name", HelpCell_ShortName, NULL, qtrue, -1, 32 );
	BE_TableDefineColumn( t, "Full name", HelpCell_LongName, NULL, qtrue, -1, 64 );
	BE_TableDefineColumn( t, "Description", HelpCell_Description, NULL, qtrue, -1, 256 );
	if ( buf )
	{
		BE_TableWrite( t, buf, 16384, qtrue, -1 );
		StatsOut( id, buf );
		free( buf );
	}
	BE_TableDestroy( t );
}

// PrintHelpForStatsTable
static void PrintHelpForStatsTable( statsTableType_t type, int id )
{
	beTable_t *t = BE_TableInit( qtrue );
	const statsHelp_t *gotOne = NULL;
	size_t i;

	if ( !t )
		return;
	for ( i = 0; i < ARRAY_LEN( helps ); i++ )
	{
		if ( helps[i].type == type )
		{
			gotOne = &helps[i];
			BE_TableDefineRow( t, (void *)&helps[i] );
		}
	}
	if ( !gotOne )
	{
		BE_TableDestroy( t );
		return;
	}
	StatsOutf( id, "%s stats:\n", HelpCell_Category( (void *)gotOne, NULL ) );
	PrintHelpTable( t, id, qfalse );
}

// PrintHelpForIndividualStat
static qboolean PrintHelpForIndividualStat( const char *query, int id )
{
	beTable_t *t = BE_TableInit( qtrue );
	qboolean gotOne = qfalse;
	size_t i;

	if ( !t )
		return qfalse;
	for ( i = 0; i < ARRAY_LEN( helps ); i++ )
	{
		if ( Q_stristr( helps[i].shortName, query ) || Q_stristr( helps[i].longName, query ) )
		{
			BE_TableDefineRow( t, (void *)&helps[i] );
			gotOne = qtrue;
		}
	}
	if ( !gotOne )
	{
		BE_TableDestroy( t );
		return qfalse;
	}
	PrintHelpTable( t, id, qtrue );
	return qtrue;
}

/*
================================================================================
Stats_Print, GetStatsFromString, the commands
================================================================================
*/

// CapCallback: row = team, column = capture index (-1 = the row label)
static const char *Cell_Capture( void *rowContext, void *columnContext )
{
	int team = *(int *)rowContext;
	int num = *(int *)columnContext;
	int i, myScore = 0, enemyScore = 0, scoreDiff, secs;

	if ( num < 0 )
		return team == TEAM_RED ? "^1Red caps:" : "^4Blue caps:";
	if ( num >= numCaptures || captures[num].capturingTeam != team )
		return NULL;
	for ( i = 0; i <= num; i++ )
	{
		if ( captures[i].capturingTeam == team )
			++myScore;
		else
			++enemyScore;
	}
	scoreDiff = myScore - enemyScore;
	secs = captures[num].time / 1000;
	if ( !scoreDiff )
		return va( "^7%d:%02d^9(=)", secs / 60, secs % 60 );
	return va( "^7%d:%02d^9(%s%d)", secs / 60, secs % 60, scoreDiff > 0 ? "+" : "", scoreDiff );
}

// GetCapturesString: a capture timeline, written to buf (empty if none)
static void GetCapturesString( char *buf, size_t bufSize )
{
	static int capIndex[MAX_CAPTURES_TRACKED], labelIndex = -1;
	int rowTeams[2], i, len;
	beTable_t *t;

	buf[0] = '\0';
	if ( !g_statsCaptureTimes.integer || numCaptures <= 0 )
		return;
	t = BE_TableInit( qfalse );
	if ( !t )
		return;
	rowTeams[0] = level.teamScores[TEAM_BLUE] > level.teamScores[TEAM_RED] ? TEAM_BLUE : TEAM_RED;
	rowTeams[1] = OtherTeam( rowTeams[0] );
	BE_TableDefineRow( t, &rowTeams[0] );
	BE_TableDefineRow( t, &rowTeams[1] );
	BE_TableDefineColumn( t, "", Cell_Capture, &labelIndex, qfalse, -1, 8 );
	for ( i = 0; i < numCaptures; i++ )
	{
		capIndex[i] = i;
		BE_TableDefineColumn( t, "0:00", Cell_Capture, &capIndex[i], qfalse, -1, 8 );
	}
	buf[0] = '\n';
	BE_TableWrite( t, buf + 1, bufSize - 1, qfalse, -1 );
	BE_TableDestroy( t );
	len = strlen( buf );
	if ( len > 0 && buf[len - 1] == '\n' )
		buf[len - 1] = '\0';
}

static qboolean ValidTeamStats( const beStats_t *s )
{
	return ( StatsValid( s ) && ( s->lastTeam == TEAM_RED || s->lastTeam == TEAM_BLUE ) ) ? qtrue : qfalse;
}

static qboolean NameMatches( const char *name, const char *query, qboolean partial )
{
	char cleanName[MAX_NETNAME], cleanQuery[MAX_STRING_CHARS];

	Q_strncpyz( cleanName, VALIDSTRING( name ) ? name : "Padawan", sizeof( cleanName ) );
	Q_StripColor( cleanName );
	Q_strncpyz( cleanQuery, query, sizeof( cleanQuery ) );
	Q_StripColor( cleanQuery );
	return ( partial ? Q_stristr( cleanName, cleanQuery ) != NULL : !Q_stricmp( cleanName, cleanQuery ) ) ? qtrue : qfalse;
}

// GetStatsFromString: client number, exact name, partial name; players in game first, then leavers
static beStats_t *GetStatsFromString( const char *str )
{
	int i, pass;
	beIter_t iter;

	if ( !VALIDSTRING( str ) )
		return NULL;

	if ( Q_isanumber( str ) )
	{
		int num = atoi( str );
		if ( num < 0 || num >= MAX_CLIENTS )
			return NULL;
		if ( g_entities[num].inuse && g_entities[num].client && clientStats[num] && clientStats[num]->lastTeam == g_entities[num].client->sess.sessionTeam &&
			ValidTeamStats( clientStats[num] ) )
			return clientStats[num];
		BE_ListIterate( &statsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *s = (beStats_t *)BE_IterNext( &iter );
			if ( s->clientNum == num && ValidTeamStats( s ) )
				return s;
		}
		return NULL;
	}

	for ( pass = 0; pass < 2; pass++ )
	{
		qboolean partial = pass ? qtrue : qfalse;

		for ( i = 0; i < MAX_CLIENTS; i++ )
		{
			gentity_t *ent = &g_entities[i];
			if ( ent->inuse && ent->client && ent->client->pers.connected != CON_DISCONNECTED && clientStats[i] &&
				clientStats[i]->lastTeam == ent->client->sess.sessionTeam && ValidTeamStats( clientStats[i] ) &&
				NameMatches( ent->client->pers.netname, str, partial ) )
				return clientStats[i];
		}
		BE_ListIterate( &statsList, &iter );
		while ( BE_IterHasNext( &iter ) )
		{
			beStats_t *s = (beStats_t *)BE_IterNext( &iter );
			if ( ValidTeamStats( s ) && NameMatches( s->name, str, partial ) )
				return s;
		}
	}
	return NULL; // no match
}

static void Stats_PrintWeaponPair( int id, beStats_t *s )
{
	PrintTeamStats( id, STATS_TABLE_WEAPON_GIVEN, s );
	PrintTeamStats( id, STATS_TABLE_WEAPON_TAKEN, s );
}

// Stats_Print: type is general/damage/weapon/accuracy/help
static void Stats_Print( int id, const char *type, beStats_t *weaponStatsPtr )
{
	if ( !VALIDSTRING( type ) )
		return;

	if ( !g_beStats.integer )
	{
		StatsOut( id, "^7Statistics are off (g_beStats 0).\n" );
		return;
	}
	if ( level.gametype != GT_CTF )
	{
		StatsOut( id, "^7Gametype is not CTF. Statistics aren't generated.\n" );
		return;
	}
	if ( !statsList.size )
	{
		StatsOut( id, "^7Nobody is playing. Statistics aren't generated.\n" );
		return;
	}

	if ( !Q_stricmpn( type, "ge", 2 ) )
	{ // general and force, with the score
		team_t winningTeam = level.teamScores[TEAM_BLUE] > level.teamScores[TEAM_RED] ? TEAM_BLUE : TEAM_RED;
		team_t losingTeam = (team_t)OtherTeam( winningTeam );
		int ms = level.intermissiontime ? ( level.intermissiontime - level.startTime ) : ( level.time - level.startTime );
		int secs = ms / 1000;
		char capsStr[8192];

		GetCapturesString( capsStr, sizeof( capsStr ) );
		StatsOutf( id, "%s: " S_COLOR_WHITE "%d    %s: " S_COLOR_WHITE "%d\n%d:%02d%s%s\n",
			winningTeam == TEAM_BLUE ? S_COLOR_BLUE "Blue" : S_COLOR_RED "Red", level.teamScores[winningTeam],
			losingTeam == TEAM_BLUE ? S_COLOR_BLUE "Blue" : S_COLOR_RED "Red", level.teamScores[losingTeam],
			secs / 60, secs % 60,
			level.intermissiontime ? "" : " elapsed",
			capsStr[0] ? capsStr : "\n" );
		PrintTeamStats( id, STATS_TABLE_GENERAL, NULL );
		PrintTeamStats( id, STATS_TABLE_FORCE, NULL );
	}
	else if ( !Q_stricmpn( type, "da", 2 ) || !Q_stricmpn( type, "dmg", 3 ) )
	{
		PrintTeamStats( id, STATS_TABLE_DAMAGE, NULL );
	}
	else if ( !Q_stricmpn( type, "we", 2 ) || !Q_stricmpn( type, "wpn", 3 ) )
	{
		if ( weaponStatsPtr )
		{ // a single player
			Stats_PrintWeaponPair( id, weaponStatsPtr );
		}
		else
		{ // everyone, by team and scoreboard order as the tables
			team_t winningTeam = level.teamScores[TEAM_BLUE] > level.teamScores[TEAM_RED] ? TEAM_BLUE : TEAM_RED;
			beList_t gotPlayersList = { 0 };
			beIter_t iter;
			int i, j, pass;

			for ( j = 0; j < 2; j++ )
			{
				team_t team = !j ? winningTeam : (team_t)OtherTeam( winningTeam );

				for ( pass = 0; pass < 3; pass++ )
				{
					for ( i = 0; i < ( pass ? 1 : level.numConnectedClients ); i++ )
					{
						BE_ListIterate( pass == 2 ? &savedStatsList : &statsList, &iter );
						while ( BE_IterHasNext( &iter ) )
						{
							beStats_t *found = (beStats_t *)BE_IterNext( &iter );
							gotPlayer_t *add;

							if ( !StatsValid( found ) || found->lastTeam != team || ( !pass && found->clientNum != level.sortedClients[i] ) )
								continue;
							if ( BE_ListFind( &gotPlayersList, PlayerMatches, found ) )
								continue;
							add = (gotPlayer_t *)BE_ListAdd( &gotPlayersList, sizeof( gotPlayer_t ) );
							if ( !add )
								continue;
							add->stats = found;
							add->sessionId = found->sessionId;
							add->isBot = found->isBot;
							add->team = found->lastTeam;
							Stats_PrintWeaponPair( id, found );
							if ( !pass )
								break;
						}
					}
				}
			}
			BE_ListClear( &gotPlayersList );
		}
	}
	else if ( !Q_stricmpn( type, "ac", 2 ) )
	{
		PrintTeamStats( id, STATS_TABLE_ACCURACY, NULL );
	}
	else if ( !Q_stricmp( type, "help" ) )
	{
		char query[MAX_STRING_CHARS] = { 0 };

		if ( trap->Argc() >= 3 )
			trap->Argv( 2, query, sizeof( query ) );
		if ( query[0] )
		{ // any new stat must not conflict with these category subcommands
			if ( !Q_stricmp( query, "all" ) )
			{
				PrintHelpForStatsTable( STATS_TABLE_GENERAL, id );
				PrintHelpForStatsTable( STATS_TABLE_WEAPON_GIVEN, id );
				PrintHelpForStatsTable( STATS_TABLE_ACCURACY, id );
			}
			else if ( !Q_stricmp( query, "gen" ) || !Q_stricmp( query, "general" ) )
				PrintHelpForStatsTable( STATS_TABLE_GENERAL, id );
			else if ( !Q_stricmp( query, "weapon" ) || !Q_stricmp( query, "wpn" ) )
				PrintHelpForStatsTable( STATS_TABLE_WEAPON_GIVEN, id );
			else if ( !Q_stricmp( query, "accuracy" ) )
				PrintHelpForStatsTable( STATS_TABLE_ACCURACY, id );
			else if ( !PrintHelpForIndividualStat( query, id ) )
				StatsOutf( id, "No help found for '%s^7'.\n"
					"Usage: ^5stats help <query>^7\n"
					"Query can be a stat name (e.g. FcDmg) or type (general/accuracy/weapon) or 'all'\n", query );
		}
		else
		{
			StatsOut( id, "Usage: ^5stats help <query>^7\n"
				"Query can be a stat name (e.g. FcDmg) or type (general/accuracy/weapon) or 'all'\n" );
		}
	}
	else
	{
		StatsOutf( id, "Unknown argument '%s^7'.\nUsage: ^5stats <gen | dmg | acc | wpn [player]>^7\nEnter ^5stats help^7 for more information about a stat.\n", type );
	}
}

// the stats a red/blue player's own "stats wpn" shows
static beStats_t *OwnWeaponStats( gentity_t *ent )
{
	int clientNum = ent - g_entities;

	if ( ent->client && ( ent->client->sess.sessionTeam == TEAM_RED || ent->client->sess.sessionTeam == TEAM_BLUE ) &&
		clientNum >= 0 && clientNum < MAX_CLIENTS && clientStats[clientNum] &&
		clientStats[clientNum]->lastTeam == ent->client->sess.sessionTeam )
		return clientStats[clientNum];
	return NULL;
}

// Cmd_PrintStats_f: stats [gen | dmg | acc | wpn [player | all] | help [query]]
void BE_Cmd_Stats_f( gentity_t *ent )
{
	int id = ent - g_entities;
	char subcmd[MAX_STRING_CHARS] = { 0 };

	if ( trap->Argc() < 2 )
	{
		Stats_Print( id, "general", NULL );
		return;
	}
	trap->Argv( 1, subcmd, sizeof( subcmd ) );
	if ( Q_stricmpn( subcmd, "we", 2 ) && Q_stricmpn( subcmd, "wpn", 3 ) )
	{
		Stats_Print( id, subcmd, NULL );
		return;
	}

	if ( trap->Argc() >= 3 )
	{
		char weaponPlayerArg[MAX_STRING_CHARS] = { 0 };

		trap->Argv( 2, weaponPlayerArg, sizeof( weaponPlayerArg ) );
		if ( weaponPlayerArg[0] )
		{
			beStats_t *found;

			if ( !Q_stricmp( weaponPlayerArg, "all" ) || !Q_stricmp( weaponPlayerArg, "-1" ) )
			{
				Stats_Print( id, subcmd, NULL );
				return;
			}
			found = GetStatsFromString( weaponPlayerArg );
			if ( !found )
			{
				StatsOutf( id, "Client %s^7 not found or ambiguous. Use client number or be more specific.\n", weaponPlayerArg );
				return;
			}
			Stats_Print( id, subcmd, found );
			return;
		}
	}
	// an in-game player with no argument: just himself
	Stats_Print( id, subcmd, OwnWeaponStats( ent ) );
}

// Svcmd_CtfStats_f: the same from rcon or the server console
void BE_Svcmd_Stats_f( void )
{
	char subcmd[MAX_STRING_CHARS] = { 0 }, weaponPlayerArg[MAX_STRING_CHARS] = { 0 };

	if ( trap->Argc() < 2 )
	{
		Stats_Print( BE_OUT_CONSOLE, "general", NULL );
		return;
	}
	trap->Argv( 1, subcmd, sizeof( subcmd ) );
	if ( trap->Argc() >= 3 && ( !Q_stricmpn( subcmd, "we", 2 ) || !Q_stricmpn( subcmd, "wpn", 3 ) ) )
	{
		trap->Argv( 2, weaponPlayerArg, sizeof( weaponPlayerArg ) );
		if ( weaponPlayerArg[0] && Q_stricmp( weaponPlayerArg, "all" ) && Q_stricmp( weaponPlayerArg, "-1" ) )
		{
			beStats_t *found = GetStatsFromString( weaponPlayerArg );
			if ( !found )
			{
				StatsOutf( BE_OUT_CONSOLE, "Client %s^7 not found or ambiguous. Use client number or be more specific.\n", weaponPlayerArg );
				return;
			}
			Stats_Print( BE_OUT_CONSOLE, subcmd, found );
			return;
		}
	}
	Stats_Print( BE_OUT_CONSOLE, subcmd, NULL );
}

/*
================================================================================
Match end, init, shutdown (upstream BeginIntermission, LogExit, G_InitGame, G_ShutdownGame)
================================================================================
*/

// BeginIntermission: count the holds still running, confirm positions, and
// show general/damage/accuracy to everyone and each human his own weapon
// tables (g_autoStats); the server console gets everything, colours stripped
void BE_StatsIntermission( void )
{
	int i;

	if ( !BE_StatsActive() )
		return;

	if ( !holdsFinalized )
	{ // LogExit: carriers at the end
		holdsFinalized = qtrue;
		for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ )
		{
			gentity_t *ent = &g_entities[i];
			beStats_t *s;
			int thisFlaghold;

			if ( !ent->inuse || !ent->client || ent->client->pers.connected != CON_CONNECTED ||
				!( ent->client->ps.powerups[PW_BLUEFLAG] || ent->client->ps.powerups[PW_REDFLAG] ) )
				continue;
			s = StatsForEnt( ent );
			if ( !s )
				continue;
			thisFlaghold = level.time - ent->client->pers.teamState.flagsince;
			s->totalFlagHold += thisFlaghold;
			if ( thisFlaghold > s->longestFlagHold )
				s->longestFlagHold = thisFlaghold;
		}
	}

	FinalizeCTFPositions();
	SyncNamesOfOldBlocks( -1 );

	if ( !g_autoStats.integer )
		return;

	Stats_Print( BE_OUT_ALL, "general", NULL );
	Stats_Print( BE_OUT_ALL, "damage", NULL );
	Stats_Print( BE_OUT_ALL, "accuracy", NULL );
	Stats_Print( BE_OUT_LOG, "weapon", NULL );

	for ( i = 0; i < level.maxclients && i < MAX_CLIENTS; i++ )
	{ // each human his own weapon tables
		gentity_t *ent = &g_entities[i];
		beStats_t *s;

		if ( !ent->inuse || ( ent->r.svFlags & SVF_BOT ) || level.clients[i].pers.connected == CON_DISCONNECTED )
			continue;
		s = OwnWeaponStats( ent );
		if ( s && StatsValid( s ) )
			Stats_Print( i, "weapon", s );
	}
}

// G_ShutdownGame: frees everything
void BE_StatsShutdown( void )
{
	BE_StatsClearStatsList( &statsList );
	BE_StatsClearStatsList( &savedStatsList );
	BE_ListClear( &identityList );
	ClearBestStats();
}

// G_InitGame: a new map or map_restart starts empty (upstream level memset)
void BE_StatsInit( void )
{
	BE_StatsShutdown();
	nextSessionId = 0;
	statBlock = 0;
	memset( clientStats, 0, sizeof( clientStats ) );
	memset( clientSessionId, 0, sizeof( clientSessionId ) );
	memset( clientIsBot, 0, sizeof( clientIsBot ) );
	memset( killedAlliedFlagCarrier, 0, sizeof( killedAlliedFlagCarrier ) );
	memset( clientWasAlive, 0, sizeof( clientWasAlive ) );
	memset( clientSpawnTime, 0, sizeof( clientSpawnTime ) );
	memset( lastHitscanHit, 0, sizeof( lastHitscanHit ) );
	memset( fcKiller, 0, sizeof( fcKiller ) );
	numTeamTicks = numRedPlayerTicks = numBluePlayerTicks = 0;
	pugConfirmed = qfalse;
	pugConfirmedPlayersPerTeam = 0;
	numCaptures = 0;
	holdsFinalized = qfalse;
	memset( golanPairId, 0, sizeof( golanPairId ) );
	memset( golanTwin, 0, sizeof( golanTwin ) );
	nextGolanPairId = 0;
	pendingGolanBall = -1;
	memset( directHitTime, 0, sizeof( directHitTime ) );
	teHighest = 0;
	flagstandsChecked = flagstandsValid = qfalse;
	existingWeaponSpawns = 0;
}
