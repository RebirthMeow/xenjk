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

// cg_director.c -- shot files for filming in the engine. A shot file (director/<name>.txt) holds camera keys and
// actors; "director play <name>" runs it on the game clock, so a demo played back with the same file gives the same
// frames every time. Display only: actors are client-side Ghoul2 models with no collision and no server entity.
//
// Shot file, one statement a line, // comments, times in seconds from the start of the shot:
//   cam <t> <x> <y> <z> <yaw> <pitch> <roll> <fov> [cut]     a camera key (fov 0: the game's own); "cut": jump to
//                                                            this key instead of gliding into it
//   interp smooth|linear                                     how the camera moves between keys (default smooth)
//   at <t> cam look point <x> <y> <z> | actor <id> [part] | client <n> [part] | off   aim the camera at something
//                                                            part: head (default), chest, origin, or a height
//                                                            above the body's origin. head and chest follow the
//                                                            bones, so they are right for any pose
//   at <t> cam rel client <n> | actor <id> | off             camera keys become offsets from that body (yaw only)
//   actor <id> <model> [skin <name>] [scale <f>]             models/players/<model>/model.glm
//   frame <ox> <oy> <oz> <scale> | frame off                 positions on the lines after it are in another frame:
//                                                            game = ( given - o ) * scale (a map's source units)
//   at <t> <id> pos <x> <y> <z> <yaw> [drop]                 put the actor there (a player origin, as viewpos prints);
//                                                            drop: stand it on the ground under that point
//   at <t> <id> face point <x> <y> <z> | cam | actor <id> | client <n> | off   turn the whole body to it
//   at <t> <id> anim <NAME> [loop|hold] [blend <ms>] [speed <f>]           an animation.cfg row, whole body
//   at <t> <id> frames <first> <last> [loop|hold] [blend <ms>] [speed <f>] a raw frame range of the model's GLA
//   at <t> <id> move <x> <y> <z> <seconds> [drop]            glide there, facing the way it goes
//   at <t> <id> look point <x> <y> <z> | cam | actor <id> [part] | client <n> [part] | off   turn the head and neck
//   at <t> <id> hide | show
//   player hide                                              do not draw the local player's own body in this shot
//   length <t>                                               the shot ends here (default: the last key or event)
//   start <console command>                                  run when the shot starts (for example video shot01)
//   end <console command>                                    run when the shot ends (for example stopvideo)
// start and end commands go to the back of the console's queue: a script that plays a shot must not leave "wait"
// commands queued behind it, or they run first.
//
// Commands: director play <name> [from] | arm <name> <clock ms> | show <name> <t> | check <name> [step] |
// floors <x> <y> [z] | stop | seek <t> | pause | time | key [t] | here <id> <model> [ANIM].
// "check" traces the loaded shot against the map and prints what is wrong before anything is rendered: where the camera
// is in or through a wall or cannot see its target, every floor under each actor, blocked walks, heads turned too far. "arm" starts the shot when the game clock (director time) reaches that value, which in a
// demo is the same moment on every playback. "show" holds the shot still at one time, for a screenshot.
// "key" and "here" append a ready line to director/keys.txt from where the view or the player is now.

#include "cg_local.h"
#include "ghoul2/G2.h"

extern stringID_table_t animTable[MAX_ANIMATIONS+1];
qboolean CG_CullPointAndRadius( const vec3_t pt, float radius );
int CG_G2SkelForModel( void *g2 );
void CG_DoSaber( vec3_t origin, vec3_t dir, float length, float lengthMax, float radius, saber_colors_t color, int rfx, qboolean doLight );

#define DIR_MAX_KEYS	512
#define DIR_MAX_ACTORS	48
#define DIR_MAX_EVENTS	2048
#define DIR_FILE_MAX	( 256 * 1024 )
#define DIR_ARGS		16
#define DIR_MAX_LIGHTS	16
#define DIR_HILT		"models/weapons2/saber/saber_w.glm"

typedef enum { DT_NONE, DT_POINT, DT_CAM, DT_ACTOR, DT_CLIENT } dirTarget_t;
typedef enum { DE_POS, DE_ANIM, DE_MOVE, DE_LOOK, DE_FACE, DE_HIDE, DE_SHOW, DE_CAMLOOK, DE_CAMREL, DE_SABER } dirEventType_t;
typedef enum { DP_HEAD, DP_CHEST, DP_HEIGHT, DP_ORIGIN } dirPart_t;

typedef struct {
	float		t;
	vec3_t		org, ang;
	float		fov;
	qboolean	cut;
} dirKey_t;

typedef struct {
	float			t;
	int				actor;			// -1: the camera
	dirEventType_t	type;
	vec3_t			v;				// pos, move target, look point
	float			f;				// yaw (pos), seconds (move)
	int				first, last;	// anim frames
	int				flags, blend;
	float			speed;
	dirTarget_t		target;
	int				targetId;
	dirPart_t		part;			// which part of a body target
	float			aim;			// DP_HEIGHT: how far above the body's origin
	int				line;			// in the shot file
	vec3_t			raw;			// pos, move: the point as written (before "drop")
	qboolean		dropped;
} dirEvent_t;

typedef struct {
	char		id[32];
	void		*ghoul2;
	qhandle_t	skin;
	int			animIndex;
	float		scale;
	int			animEvent;			// the anim event playing (-1: none started)
	qboolean	looked;				// the head bones have been turned at least once
	float		lookYaw, lookPitch;
	qboolean	saber;				// carries a saber (the hilt is model 1 of ghoul2, on the right hand)
	int			saberColor, bladeBolt;
	float		saberLen;
} dirActor_t;

// a light: a world point, or an offset (ahead, left, up) from an actor or from the camera
typedef struct {
	int			rel;				// -2 world, -1 camera, else an actor
	vec3_t		v, rgb;
	float		radius;
} dirLight_t;

static struct {
	qboolean	loaded, playing, paused, ended, armed;
	int			armTime;			// cg.time at which an armed shot starts
	char		name[MAX_QPATH];
	int			startTime;			// cg.time at t = 0
	float		pauseT;
	float		length;
	qboolean	linear;
	qboolean	hidePlayer;
	char		startCmd[256], endCmd[256];
	int			numKeys, numActors, numEvents, numLights;
	dirLight_t	lights[DIR_MAX_LIGHTS];
	dirKey_t	keys[DIR_MAX_KEYS];
	dirActor_t	actors[DIR_MAX_ACTORS];
	dirEvent_t	events[DIR_MAX_EVENTS];
} dir;

static char dirFile[DIR_FILE_MAX];
static int dirDebug;

// "frame": the positions on the following lines are in another frame (a map's source units)
static struct {
	qboolean	on;
	vec3_t		org;
	float		scale;
} dirFrame, dirFrameLast;		// Last: the file's last "frame", for reporting in the same units

static void Dir_Frame( vec3_t v ) {
	if ( dirFrame.on ) {
		VectorSubtract( v, dirFrame.org, v );
		VectorScale( v, dirFrame.scale, v );
	}
}

static int Dir_Floors( const vec3_t at, float *z, int max );

// "drop": the player origin of someone standing on the ground under v (v unchanged when there is none). The floor
// taken is the highest one not more than a step above v, so a point written inside a canopy or a slab still lands on
// the floor below it; with nothing below, the lowest floor above.
static void Dir_Drop( vec3_t v, int lineNum ) {
	float	z[8];
	int		n = Dir_Floors( v, z, 8 ), i;

	if ( !n ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: no ground under %.0f %.0f %.0f (is that part of the map's collision loaded?)\n", lineNum, v[0], v[1], v[2] );
		return;
	}
	for ( i = 0; i < n; i++ ) {
		if ( z[i] <= v[2] + 24.0f ) {
			v[2] = z[i];
			return;
		}
	}
	v[2] = z[n - 1];
}

// drops are taken when a shot starts, is shown or is checked: by then the map's collision is loaded where it plays
static void Dir_ResolveDrops( void ) {
	int i;

	for ( i = 0; i < dir.numEvents; i++ ) {
		dirEvent_t *e = &dir.events[i];

		if ( ( e->type == DE_POS || e->type == DE_MOVE ) && e->dropped ) {
			VectorCopy( e->raw, e->v );
			Dir_Drop( e->v, e->line );
		}
	}
}

static void Dir_Restart( float from );

static float Dir_Time( void ) {
	float t;

	if ( dir.armed ) {
		return 0.0f;		// waiting on its first pose
	}
	if ( dir.paused ) {
		return dir.pauseT;
	}
	t = ( cg.time - dir.startTime ) * 0.001f;
	return t > 0.0f ? t : 0.0f;
}

static int Dir_FindActor( const char *id ) {
	int i;

	for ( i = 0; i < dir.numActors; i++ ) {
		if ( !Q_stricmp( dir.actors[i].id, id ) ) {
			return i;
		}
	}
	return -1;
}

static void Dir_FreeActors( void ) {
	int i;

	for ( i = 0; i < dir.numActors; i++ ) {
		if ( dir.actors[i].ghoul2 && trap->G2_HaveWeGhoul2Models( dir.actors[i].ghoul2 ) ) {
			trap->G2API_CleanGhoul2Models( &dir.actors[i].ghoul2 );
		}
		dir.actors[i].ghoul2 = NULL;
	}
	dir.numActors = 0;
}

void CG_Director_Shutdown( void ) {
	Dir_FreeActors();
	dir.loaded = dir.playing = dir.paused = dir.ended = dir.armed = qfalse;
	dir.numKeys = dir.numEvents = dir.numLights = 0;
	dirDebug = 4;
}

// from CG_Player: a shot with "player hide" leaves the local player's body (and what it carries) out
qboolean CG_Director_HidesPlayer( int clientNum ) {
	return ( dir.playing && dir.hidePlayer && cg.snap && clientNum == cg.snap->ps.clientNum ) ? qtrue : qfalse;
}

// split a line in place into at most DIR_ARGS words
static int Dir_Split( char *line, char **argv ) {
	int n = 0;

	while ( *line && n < DIR_ARGS ) {
		while ( *line == ' ' || *line == '\t' || *line == '\r' ) {
			line++;
		}
		if ( !*line || ( line[0] == '/' && line[1] == '/' ) ) {
			break;
		}
		argv[n++] = line;
		while ( *line && *line != ' ' && *line != '\t' && *line != '\r' ) {
			line++;
		}
		if ( *line ) {
			*line++ = 0;
		}
	}
	return n;
}

// "point x y z" | "cam" | "actor id" | "client n" | "off" from argv[at]; qfalse when it does not parse
static qboolean Dir_ParseTarget( char **argv, int argc, int at, dirEvent_t *e, qboolean camAllowed ) {
	if ( at >= argc ) {
		return qfalse;
	}
	if ( !Q_stricmp( argv[at], "off" ) ) {
		e->target = DT_NONE;
		return qtrue;
	}
	if ( camAllowed && !Q_stricmp( argv[at], "cam" ) ) {
		e->target = DT_CAM;
		return qtrue;
	}
	if ( !Q_stricmp( argv[at], "point" ) && at + 3 < argc ) {
		e->target = DT_POINT;
		VectorSet( e->v, atof( argv[at + 1] ), atof( argv[at + 2] ), atof( argv[at + 3] ) );
		Dir_Frame( e->v );
		return qtrue;
	}
	e->part = DP_HEAD;
	if ( at + 2 < argc ) {
		if ( !Q_stricmp( argv[at + 2], "chest" ) ) {
			e->part = DP_CHEST;
		} else if ( !Q_stricmp( argv[at + 2], "origin" ) ) {
			e->part = DP_ORIGIN;
		} else if ( Q_stricmp( argv[at + 2], "head" ) ) {
			e->part = DP_HEIGHT;
			e->aim = atof( argv[at + 2] );
		}
	}
	if ( !Q_stricmp( argv[at], "client" ) && at + 1 < argc ) {
		e->target = DT_CLIENT;
		e->targetId = atoi( argv[at + 1] );
		return e->targetId >= 0 && e->targetId < MAX_CLIENTS;
	}
	if ( !Q_stricmp( argv[at], "actor" ) && at + 1 < argc ) {
		e->target = DT_ACTOR;
		e->targetId = Dir_FindActor( argv[at + 1] );
		return e->targetId >= 0;
	}
	return qfalse;
}

// [loop|hold] [blend ms] [speed f] from argv[at] on
static void Dir_ParseAnimOptions( char **argv, int argc, int at, dirEvent_t *e, qboolean loops ) {
	e->flags = loops ? BONE_ANIM_OVERRIDE_LOOP : BONE_ANIM_OVERRIDE_FREEZE;
	e->blend = 300;
	e->speed = 1.0f;
	for ( ; at < argc; at++ ) {
		if ( !Q_stricmp( argv[at], "loop" ) ) {
			e->flags = BONE_ANIM_OVERRIDE_LOOP;
		} else if ( !Q_stricmp( argv[at], "hold" ) ) {
			e->flags = BONE_ANIM_OVERRIDE_FREEZE;
		} else if ( !Q_stricmp( argv[at], "blend" ) && at + 1 < argc ) {
			e->blend = atoi( argv[++at] );
		} else if ( !Q_stricmp( argv[at], "speed" ) && at + 1 < argc ) {
			e->speed = atof( argv[++at] );
		}
	}
	if ( e->speed <= 0.0f ) {
		e->speed = 1.0f;
	}
}

static qboolean Dir_LoadActor( char **argv, int argc, int lineNum ) {
	dirActor_t	*a;
	char		path[MAX_QPATH];
	const char	*skin = "default", *saber = NULL;
	int			i;

	if ( argc < 3 ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: actor <id> <model>\n", lineNum );
		return qfalse;
	}
	if ( dir.numActors >= DIR_MAX_ACTORS ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: more than %i actors\n", lineNum, DIR_MAX_ACTORS );
		return qfalse;
	}
	if ( Dir_FindActor( argv[1] ) >= 0 || !Q_stricmp( argv[1], "cam" ) ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: actor id '%s' is taken\n", lineNum, argv[1] );
		return qfalse;
	}
	a = &dir.actors[dir.numActors];
	memset( a, 0, sizeof( *a ) );
	Q_strncpyz( a->id, argv[1], sizeof( a->id ) );
	a->scale = 1.0f;
	a->animEvent = -1;
	for ( i = 3; i + 1 < argc; i += 2 ) {
		if ( !Q_stricmp( argv[i], "skin" ) ) {
			skin = argv[i + 1];
		} else if ( !Q_stricmp( argv[i], "scale" ) ) {
			a->scale = atof( argv[i + 1] );
		} else if ( !Q_stricmp( argv[i], "saber" ) ) {
			saber = argv[i + 1];
		}
	}
	if ( a->scale <= 0.0f ) {
		a->scale = 1.0f;
	}
	Com_sprintf( path, sizeof( path ), "models/players/%s/model_%s.skin", argv[2], skin );
	a->skin = trap->R_RegisterSkin( path );
	Com_sprintf( path, sizeof( path ), "models/players/%s/model.glm", argv[2] );
	trap->G2API_InitGhoul2Model( &a->ghoul2, path, 0, a->skin, 0, 0, 0 );
	if ( !a->ghoul2 ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: could not load '%s'\n", lineNum, path );
		return qfalse;
	}
	if ( a->skin ) {
		trap->G2API_SetSkin( a->ghoul2, 0, a->skin, a->skin );
	}
	a->animIndex = CG_G2SkelForModel( a->ghoul2 );
	if ( saber ) {
		static const char	*names[] = { "red", "orange", "yellow", "green", "blue", "purple" };
		void				*hilt = NULL;
		int					hand = trap->G2API_AddBolt( a->ghoul2, 0, "*r_hand" );

		a->saberColor = SABER_BLUE;
		for ( i = 0; i < 6; i++ ) {
			if ( !Q_stricmp( saber, names[i] ) ) {
				a->saberColor = SABER_RED + i;
			}
		}
		trap->G2API_InitGhoul2Model( &hilt, DIR_HILT, 0, 0, 0, 0, 0 );
		if ( hand < 0 || !hilt ) {
			trap->Print( S_COLOR_YELLOW "director: line %i: no saber for %s (%s)\n", lineNum, a->id, hand < 0 ? "the model has no *r_hand" : "no hilt model" );
		} else {
			a->bladeBolt = trap->G2API_AddBolt( hilt, 0, "*blade1" );
			trap->G2API_SetBoltInfo( hilt, 0, hand );
			trap->G2API_CopySpecificGhoul2Model( hilt, 0, a->ghoul2, 1 );
			a->saber = qtrue;
			a->saberLen = 40.0f;
			trap->Print( "director: %s saber: hand bolt %i, blade bolt %i, has model 1: %i\n", a->id, hand, a->bladeBolt, trap->G2API_HasGhoul2ModelOnIndex( &a->ghoul2, 1 ) );
		}
		if ( hilt ) {
			trap->G2API_CleanGhoul2Models( &hilt );
		}
	}
	dir.numActors++;
	return qtrue;
}

static qboolean Dir_ParseAt( char **argv, int argc, int lineNum ) {
	dirEvent_t	*e;
	const char	*what;

	if ( argc < 4 ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: at <t> <id> <what> ...\n", lineNum );
		return qfalse;
	}
	if ( dir.numEvents >= DIR_MAX_EVENTS ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: more than %i events\n", lineNum, DIR_MAX_EVENTS );
		return qfalse;
	}
	e = &dir.events[dir.numEvents];
	memset( e, 0, sizeof( *e ) );
	e->t = atof( argv[1] );
	e->line = lineNum;
	what = argv[3];
	if ( !Q_stricmp( argv[2], "cam" ) ) {
		e->actor = -1;
		if ( !Q_stricmp( what, "look" ) && Dir_ParseTarget( argv, argc, 4, e, qfalse ) ) {
			e->type = DE_CAMLOOK;
		} else if ( !Q_stricmp( what, "rel" ) && Dir_ParseTarget( argv, argc, 4, e, qfalse ) && e->target != DT_POINT ) {
			e->type = DE_CAMREL;
		} else {
			trap->Print( S_COLOR_YELLOW "director: line %i: cam look|rel <target>\n", lineNum );
			return qfalse;
		}
		dir.numEvents++;
		return qtrue;
	}
	e->actor = Dir_FindActor( argv[2] );
	if ( e->actor < 0 ) {
		trap->Print( S_COLOR_YELLOW "director: line %i: no actor '%s' (declare it first)\n", lineNum, argv[2] );
		return qfalse;
	}
	if ( !Q_stricmp( what, "pos" ) && argc >= 8 ) {
		e->type = DE_POS;
		VectorSet( e->v, atof( argv[4] ), atof( argv[5] ), atof( argv[6] ) );
		e->f = atof( argv[7] );
		Dir_Frame( e->v );
		VectorCopy( e->v, e->raw );
		if ( argc > 8 && !Q_stricmp( argv[8], "drop" ) ) {
			e->dropped = qtrue;
		}
	} else if ( !Q_stricmp( what, "move" ) && argc >= 8 ) {
		e->type = DE_MOVE;
		VectorSet( e->v, atof( argv[4] ), atof( argv[5] ), atof( argv[6] ) );
		e->f = atof( argv[7] );
		if ( e->f < 0.001f ) {
			e->f = 0.001f;
		}
		Dir_Frame( e->v );
		VectorCopy( e->v, e->raw );
		if ( argc > 8 && !Q_stricmp( argv[8], "drop" ) ) {
			e->dropped = qtrue;
		}
	} else if ( !Q_stricmp( what, "anim" ) && argc >= 5 ) {
		const dirActor_t	*a = &dir.actors[e->actor];
		int					num = GetIDForString( animTable, argv[4] );
		const animation_t	*an;

		if ( num < 0 || a->animIndex < 0 || !bgAllAnims[a->animIndex].anims ) {
			trap->Print( S_COLOR_YELLOW "director: line %i: no animation '%s' for %s\n", lineNum, argv[4], a->id );
			return qfalse;
		}
		an = &bgAllAnims[a->animIndex].anims[num];
		if ( an->numFrames < 1 ) {
			trap->Print( S_COLOR_YELLOW "director: line %i: animation '%s' is empty in %s's animation.cfg\n", lineNum, argv[4], a->id );
			return qfalse;
		}
		e->type = DE_ANIM;
		e->first = an->firstFrame;
		e->last = an->firstFrame + an->numFrames;
		Dir_ParseAnimOptions( argv, argc, 5, e, an->loopFrames != -1 );
		if ( an->frameLerp ) {
			e->speed *= 50.0f / fabs( (float)an->frameLerp );
		}
	} else if ( !Q_stricmp( what, "frames" ) && argc >= 6 ) {
		e->type = DE_ANIM;
		e->first = atoi( argv[4] );
		e->last = atoi( argv[5] );
		Dir_ParseAnimOptions( argv, argc, 6, e, qtrue );
	} else if ( !Q_stricmp( what, "look" ) && Dir_ParseTarget( argv, argc, 4, e, qtrue ) ) {
		e->type = DE_LOOK;
	} else if ( !Q_stricmp( what, "face" ) && Dir_ParseTarget( argv, argc, 4, e, qtrue ) ) {
		e->type = DE_FACE;
	} else if ( !Q_stricmp( what, "saber" ) && argc >= 5 ) {
		e->type = DE_SABER;
		e->f = !Q_stricmp( argv[4], "off" ) ? 0.0f : 1.0f;
	} else if ( !Q_stricmp( what, "hide" ) ) {
		e->type = DE_HIDE;
	} else if ( !Q_stricmp( what, "show" ) ) {
		e->type = DE_SHOW;
	} else {
		trap->Print( S_COLOR_YELLOW "director: line %i: cannot read '%s' for %s\n", lineNum, what, argv[2] );
		return qfalse;
	}
	dir.numEvents++;
	return qtrue;
}

static qboolean Dir_Load( const char *name ) {
	fileHandle_t	f;
	int				len, lineNum = 0, i, j, bad = 0;
	char			*p, *next, *argv[DIR_ARGS];
	float			last = 0.0f;

	CG_Director_Shutdown();
	len = trap->FS_Open( va( "director/%s.txt", name ), &f, FS_READ );
	if ( !f || len <= 0 ) {
		trap->Print( "director: no director/%s.txt\n", name );
		if ( f ) {
			trap->FS_Close( f );
		}
		return qfalse;
	}
	if ( len >= DIR_FILE_MAX ) {
		trap->Print( "director: director/%s.txt is over %i bytes\n", name, DIR_FILE_MAX );
		trap->FS_Close( f );
		return qfalse;
	}
	trap->FS_Read( dirFile, len, f );
	trap->FS_Close( f );
	dirFile[len] = 0;
	dir.length = 0.0f;
	dir.linear = qfalse;
	dir.startCmd[0] = dir.endCmd[0] = 0;
	dirFrame.on = qfalse;
	dirFrameLast.on = qfalse;
	dir.hidePlayer = qfalse;

	for ( p = dirFile; p && *p; p = next ) {
		int		argc;
		char	raw[256];

		next = strchr( p, '\n' );
		if ( next ) {
			*next++ = 0;
		}
		lineNum++;
		Q_strncpyz( raw, p, sizeof( raw ) );
		argc = Dir_Split( p, argv );
		if ( !argc ) {
			continue;
		}
		if ( !Q_stricmp( argv[0], "cam" ) ) {
			dirKey_t *k;

			if ( argc < 9 || dir.numKeys >= DIR_MAX_KEYS ) {
				trap->Print( S_COLOR_YELLOW "director: line %i: cam <t> <x> <y> <z> <yaw> <pitch> <roll> <fov> [cut]\n", lineNum );
				bad++;
				continue;
			}
			k = &dir.keys[dir.numKeys++];
			k->t = atof( argv[1] );
			VectorSet( k->org, atof( argv[2] ), atof( argv[3] ), atof( argv[4] ) );
			Dir_Frame( k->org );
			k->ang[YAW] = atof( argv[5] );
			k->ang[PITCH] = atof( argv[6] );
			k->ang[ROLL] = atof( argv[7] );
			k->fov = atof( argv[8] );
			k->cut = ( argc > 9 && !Q_stricmp( argv[9], "cut" ) ) ? qtrue : qfalse;
		} else if ( !Q_stricmp( argv[0], "actor" ) ) {
			if ( !Dir_LoadActor( argv, argc, lineNum ) ) {
				bad++;
			}
		} else if ( !Q_stricmp( argv[0], "at" ) ) {
			if ( !Dir_ParseAt( argv, argc, lineNum ) ) {
				bad++;
			}
		} else if ( !Q_stricmp( argv[0], "frame" ) && argc >= 2 ) {
			dirFrame.on = ( argc >= 5 ) ? qtrue : qfalse;
			if ( dirFrame.on ) {
				VectorSet( dirFrame.org, atof( argv[1] ), atof( argv[2] ), atof( argv[3] ) );
				dirFrame.scale = atof( argv[4] );
				dirFrameLast = dirFrame;
			}
		} else if ( !Q_stricmp( argv[0], "light" ) ) {
			// light <x> <y> <z> <radius> <r> <g> <b> | light actor <id> <ahead> <left> <up> <radius> <r> <g> <b>
			// | light cam <ahead> <left> <up> <radius> <r> <g> <b>; lengths in the frame's units
			dirLight_t	*l = &dir.lights[dir.numLights];
			int			a = 1;
			float		sc = dirFrame.on ? dirFrame.scale : 1.0f;

			l->rel = -2;
			if ( argc > 1 && !Q_stricmp( argv[1], "cam" ) ) {
				l->rel = -1;
				a = 2;
			} else if ( argc > 2 && !Q_stricmp( argv[1], "actor" ) ) {
				l->rel = Dir_FindActor( argv[2] );
				a = 3;
			}
			if ( argc < a + 7 || dir.numLights >= DIR_MAX_LIGHTS || ( a == 3 && l->rel < 0 ) ) {
				trap->Print( S_COLOR_YELLOW "director: line %i: light [cam | actor <id>] <x> <y> <z> <radius> <r> <g> <b> (up to %i)\n", lineNum, DIR_MAX_LIGHTS );
				bad++;
				continue;
			}
			VectorSet( l->v, atof( argv[a] ), atof( argv[a + 1] ), atof( argv[a + 2] ) );
			if ( l->rel == -2 ) {
				Dir_Frame( l->v );
			} else {
				VectorScale( l->v, sc, l->v );
			}
			l->radius = atof( argv[a + 3] ) * sc;
			VectorSet( l->rgb, atof( argv[a + 4] ), atof( argv[a + 5] ), atof( argv[a + 6] ) );
			dir.numLights++;
		} else if ( !Q_stricmp( argv[0], "player" ) && argc >= 2 ) {
			dir.hidePlayer = !Q_stricmp( argv[1], "hide" ) ? qtrue : qfalse;
		} else if ( !Q_stricmp( argv[0], "length" ) && argc >= 2 ) {
			dir.length = atof( argv[1] );
		} else if ( !Q_stricmp( argv[0], "interp" ) && argc >= 2 ) {
			dir.linear = !Q_stricmp( argv[1], "linear" ) ? qtrue : qfalse;
		} else if ( !Q_stricmp( argv[0], "start" ) && argc >= 2 ) {
			const char *cmd = strstr( raw, "start" );

			Q_strncpyz( dir.startCmd, cmd ? cmd + 5 : "", sizeof( dir.startCmd ) );
		} else if ( !Q_stricmp( argv[0], "end" ) && argc >= 2 ) {
			const char *cmd = strstr( raw, "end" );

			Q_strncpyz( dir.endCmd, cmd ? cmd + 3 : "", sizeof( dir.endCmd ) );
		} else {
			trap->Print( S_COLOR_YELLOW "director: line %i: unknown statement '%s'\n", lineNum, argv[0] );
			bad++;
		}
	}

	// keys and events in time order (stable: lines at the same time keep the file's order)
	for ( i = 1; i < dir.numKeys; i++ ) {
		dirKey_t k = dir.keys[i];

		for ( j = i; j > 0 && dir.keys[j - 1].t > k.t; j-- ) {
			dir.keys[j] = dir.keys[j - 1];
		}
		dir.keys[j] = k;
	}
	for ( i = 1; i < dir.numEvents; i++ ) {
		dirEvent_t e = dir.events[i];

		for ( j = i; j > 0 && dir.events[j - 1].t > e.t; j-- ) {
			dir.events[j] = dir.events[j - 1];
		}
		dir.events[j] = e;
	}
	// angles run on from key to key, so a turn through 180 degrees takes the short way
	for ( i = 1; i < dir.numKeys; i++ ) {
		if ( dir.keys[i].cut ) {
			continue;
		}
		for ( j = 0; j < 3; j++ ) {
			dir.keys[i].ang[j] = dir.keys[i - 1].ang[j] + AngleSubtract( dir.keys[i].ang[j], dir.keys[i - 1].ang[j] );
		}
	}
	if ( dir.numKeys ) {
		last = dir.keys[dir.numKeys - 1].t;
	}
	for ( i = 0; i < dir.numEvents; i++ ) {
		float end = dir.events[i].t + ( dir.events[i].type == DE_MOVE ? dir.events[i].f : 0.0f );

		if ( end > last ) {
			last = end;
		}
	}
	if ( dir.length <= 0.0f ) {
		dir.length = last;
	}
	Q_strncpyz( dir.name, name, sizeof( dir.name ) );
	dir.loaded = qtrue;
	trap->Print( "director: %s: %i camera keys, %i actors, %i events, %.2f s%s\n", name, dir.numKeys, dir.numActors,
		dir.numEvents, dir.length, bad ? va( ", %i lines skipped", bad ) : "" );
	return qtrue;
}

// where an actor stands at t; qfalse while it has not been placed or is hidden
static qboolean Dir_TargetPoint( const dirEvent_t *e, float t, vec3_t out, float *yaw, qboolean part, qboolean face );

// a bone's place in the world for a model standing at org, turned to yaw
static qboolean Dir_BonePoint( void *ghoul2, const char *bone, const vec3_t org, float yaw, float scale, vec3_t out ) {
	mdxaBone_t	m;
	vec3_t		ang, sc;
	int			bolt;

	if ( !ghoul2 ) {
		return qfalse;
	}
	bolt = trap->G2API_AddBolt( ghoul2, 0, bone );
	if ( bolt < 0 ) {
		return qfalse;
	}
	VectorSet( ang, 0.0f, yaw, 0.0f );
	VectorSet( sc, scale, scale, scale );
	if ( !trap->G2API_GetBoltMatrix( ghoul2, 0, bolt, &m, ang, org, cg.time, cgs.gameModels, sc ) ) {
		return qfalse;
	}
	BG_GiveMeVectorFromMatrix( &m, ORIGIN, out );
	return qtrue;
}

// face: also turn the body to its "face" target (off while resolving someone else's facing, so two actors facing
// each other do not chase each other's answer)
static qboolean Dir_ActorPose( int actor, float t, vec3_t pos, float *yaw, qboolean face ) {
	qboolean			placed = qfalse, hidden = qfalse;
	const dirEvent_t	*facing = NULL;
	int					i;

	VectorClear( pos );
	*yaw = 0.0f;
	for ( i = 0; i < dir.numEvents && dir.events[i].t <= t; i++ ) {
		const dirEvent_t *e = &dir.events[i];

		if ( e->actor != actor ) {
			continue;
		}
		if ( e->type == DE_POS ) {
			VectorCopy( e->v, pos );
			*yaw = e->f;
			placed = qtrue;
			facing = NULL;
		} else if ( e->type == DE_FACE ) {
			facing = e->target == DT_NONE ? NULL : e;
		} else if ( e->type == DE_MOVE && placed ) {
			vec3_t	d;

			facing = NULL;
			float	k = ( t - e->t ) / e->f;

			VectorSubtract( e->v, pos, d );
			if ( d[0] * d[0] + d[1] * d[1] > 1.0f ) {
				*yaw = atan2( d[1], d[0] ) * ( 180.0f / M_PI );
			}
			if ( k > 1.0f ) {
				k = 1.0f;
			}
			VectorMA( pos, k, d, pos );
		} else if ( e->type == DE_HIDE ) {
			hidden = qtrue;
		} else if ( e->type == DE_SHOW ) {
			hidden = qfalse;
		}
	}
	if ( face && facing && placed ) {
		vec3_t at;

		if ( Dir_TargetPoint( facing, t, at, NULL, qfalse, qfalse ) && ( at[0] != pos[0] || at[1] != pos[1] ) ) {
			*yaw = atan2( at[1] - pos[1], at[0] - pos[0] ) * ( 180.0f / M_PI );
		}
	}
	return placed && !hidden;
}

// the world point a target names; qfalse when it is not there to look at
// part: the body part the event names instead of the body's origin
static qboolean Dir_TargetPoint( const dirEvent_t *e, float t, vec3_t out, float *yaw, qboolean part, qboolean face ) {
	float		y = 0.0f, scale = 1.0f;
	void		*ghoul2 = NULL;
	vec3_t		org;

	switch ( e->target ) {
	case DT_POINT:
		VectorCopy( e->v, out );
		break;
	case DT_CAM:
		VectorCopy( cg.refdef.vieworg, out );
		break;
	case DT_ACTOR:
		if ( !Dir_ActorPose( e->targetId, t, out, &y, face ) ) {
			return qfalse;
		}
		ghoul2 = dir.actors[e->targetId].ghoul2;
		scale = dir.actors[e->targetId].scale;
		break;
	case DT_CLIENT:
		if ( cg.snap && e->targetId == cg.snap->ps.clientNum ) {
			VectorCopy( cg.predictedPlayerState.origin, out );
			y = cg.predictedPlayerState.viewangles[YAW];
		} else if ( cg_entities[e->targetId].currentValid ) {
			VectorCopy( cg_entities[e->targetId].lerpOrigin, out );
			y = cg_entities[e->targetId].lerpAngles[YAW];
		} else {
			return qfalse;
		}
		ghoul2 = cg_entities[e->targetId].ghoul2;
		break;
	default:
		return qfalse;
	}
	if ( yaw ) {
		*yaw = y;
	}
	if ( part && ( e->target == DT_ACTOR || e->target == DT_CLIENT ) ) {
		VectorCopy( out, org );
		if ( e->part == DP_HEIGHT ) {
			out[2] += e->aim;
		} else if ( e->part != DP_ORIGIN
			&& !Dir_BonePoint( ghoul2, e->part == DP_CHEST ? "thoracic" : "cranium", org, y, scale, out ) ) {
			out[2] = org[2] + ( e->part == DP_CHEST ? 20.0f : 38.0f ) * scale;	// no such bone: a standing body's
		}
	}
	return qtrue;
}

static float Dir_Spline( float p0, float p1, float p2, float p3, float u ) {
	if ( dir.linear ) {
		return p1 + ( p2 - p1 ) * u;
	}
	return 0.5f * ( 2.0f * p1 + ( -p0 + p2 ) * u + ( 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 ) * u * u
		+ ( -p0 + 3.0f * p1 - 3.0f * p2 + p3 ) * u * u * u );
}

static void Dir_CameraAt( float t, vec3_t org, vec3_t ang, float *fov ) {
	const dirKey_t	*k0, *k1, *k2, *k3;
	int				i, j;
	float			u;

	for ( i = 0; i + 1 < dir.numKeys && dir.keys[i + 1].t <= t; i++ ) {
	}
	k1 = &dir.keys[i];
	if ( i + 1 >= dir.numKeys || dir.keys[i + 1].cut || t <= k1->t ) {
		// before the first key, after the last, or waiting for a cut: hold
		VectorCopy( k1->org, org );
		VectorCopy( k1->ang, ang );
		*fov = k1->fov;
		return;
	}
	k2 = &dir.keys[i + 1];
	k0 = ( i > 0 && !k1->cut ) ? &dir.keys[i - 1] : k1;
	k3 = ( i + 2 < dir.numKeys && !dir.keys[i + 2].cut ) ? &dir.keys[i + 2] : k2;
	u = ( t - k1->t ) / ( k2->t - k1->t );
	for ( j = 0; j < 3; j++ ) {
		org[j] = Dir_Spline( k0->org[j], k1->org[j], k2->org[j], k3->org[j], u );
		ang[j] = Dir_Spline( k0->ang[j], k1->ang[j], k2->ang[j], k3->ang[j], u );
	}
	if ( k1->fov > 0.0f && k2->fov > 0.0f ) {
		*fov = Dir_Spline( k0->fov > 0.0f ? k0->fov : k1->fov, k1->fov, k2->fov, k3->fov > 0.0f ? k3->fov : k2->fov, u );
	} else {
		*fov = k1->fov > 0.0f ? k1->fov : k2->fov;
	}
}

// the view at t: the keyed path, then "cam rel" and "cam look". qtrue when the camera is aimed at something (lookAt)
static qboolean Dir_ViewAt( float t, vec3_t org, vec3_t ang, float *fov, vec3_t lookAt ) {
	const dirEvent_t	*look = NULL, *rel = NULL;
	vec3_t				at, d;
	float				yaw;
	int					i;

	Dir_CameraAt( t, org, ang, fov );
	for ( i = 0; i < dir.numEvents && dir.events[i].t <= t; i++ ) {
		if ( dir.events[i].type == DE_CAMLOOK ) {
			look = dir.events[i].target == DT_NONE ? NULL : &dir.events[i];
		} else if ( dir.events[i].type == DE_CAMREL ) {
			rel = dir.events[i].target == DT_NONE ? NULL : &dir.events[i];
		}
	}
	if ( rel && Dir_TargetPoint( rel, t, at, &yaw, qfalse, qtrue ) ) {
		// the keys are offsets in the body's frame (yaw only): x ahead of it, y to its left, z up
		float s = sin( yaw * ( M_PI / 180.0f ) ), c = cos( yaw * ( M_PI / 180.0f ) );
		float x = org[0], y = org[1];

		org[0] = at[0] + x * c - y * s;
		org[1] = at[1] + x * s + y * c;
		org[2] += at[2];
		ang[YAW] += yaw;
	}
	if ( look && Dir_TargetPoint( look, t, at, NULL, qtrue, qtrue ) ) {
		float roll = ang[ROLL];

		VectorSubtract( at, org, d );
		vectoangles( d, ang );
		ang[ROLL] = roll;
		VectorCopy( at, lookAt );
		return qtrue;
	}
	return qfalse;
}

// the shot's last pose stays up (camera and actors) until "director stop", so nothing else slips into a capture
static void Dir_End( void ) {
	dir.ended = qtrue;
	trap->Print( "director: %s ended at game clock %i ms\n", dir.name, cg.time );
	trap->Cvar_Set( "director_done", "1" );
	if ( dir.endCmd[0] ) {
		trap->SendConsoleCommand( va( "%s\n", dir.endCmd ) );
	}
}

// from CG_DrawActiveFrame, right after CG_CalcViewValues (and the bench camera)
void CG_Director_Camera( void ) {
	vec3_t				org, ang, at;
	float				t, fov;

	if ( !dir.playing ) {
		return;
	}
	if ( dir.armed && cg.time >= dir.armTime ) {
		Dir_Restart( 0.0f );	// t = 0 is this frame
	}
	t = Dir_Time();
	if ( t >= dir.length && !dir.paused && !dir.armed ) {
		t = dir.length;
		if ( !dir.ended ) {
			Dir_End();
		}
	}
	if ( !dir.numKeys ) {
		return;			// actors only: the game's own view
	}
	Dir_ViewAt( t, org, ang, &fov, at );
	VectorCopy( org, cg.refdef.vieworg );
	VectorCopy( ang, cg.refdef.viewangles );
	AnglesToAxis( cg.refdef.viewangles, cg.refdef.viewaxis );
	if ( fov > 0.0f ) {
		float x;

		if ( fov < 1.0f ) fov = 1.0f;
		if ( fov > 160.0f ) fov = 160.0f;
		if ( cg_fovAspectAdjust.integer ) {
			// the same widening the game gives cg_fov, so a key's fov means what cg_fov means
			fov = atan( tan( fov * M_PI / 360.0f ) * 0.75f * ( (float)cgs.glconfig.vidWidth / (float)cgs.glconfig.vidHeight ) ) * 360.0f / M_PI;
		}
		x = cg.refdef.width / tan( fov / 360.0f * M_PI );
		cg.refdef.fov_x = fov;
		cg.refdef.fov_y = atan2( cg.refdef.height, x ) * 360.0f / M_PI;
	}
	// the camera has left the player's eyes: draw the player's own body, unless the shot leaves the player out
	if ( dir.hidePlayer ) {
		cg.renderingThirdPerson = qfalse;
	} else if ( cg.snap && cg.predictedPlayerState.pm_type != PM_SPECTATOR && cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR ) {
		cg.renderingThirdPerson = qtrue;
	}
}

// every frame before the scene is rendered: pose and add the actors
void CG_Director_AddActors( void ) {
	refEntity_t	ent;
	float		t;
	int			n, i, j;

	if ( !dir.loaded || ( !dir.playing && !dir.paused ) ) {
		return;
	}
	t = Dir_Time();
	if ( t > dir.length ) {
		t = dir.length;
	}
	for ( n = 0; n < dir.numLights; n++ ) {
		const dirLight_t	*l = &dir.lights[n];
		vec3_t				org, fwd, left, ang;
		float				yaw;

		if ( l->rel == -2 ) {
			VectorCopy( l->v, org );
		} else {
			if ( l->rel == -1 ) {
				VectorCopy( cg.refdef.vieworg, org );
				VectorCopy( cg.refdef.viewaxis[0], fwd );
				VectorCopy( cg.refdef.viewaxis[1], left );
			} else {
				if ( !Dir_ActorPose( l->rel, t, org, &yaw, qtrue ) ) {
					continue;
				}
				VectorSet( ang, 0.0f, yaw, 0.0f );
				AngleVectors( ang, fwd, left, NULL );
				VectorNegate( left, left );
			}
			VectorMA( org, l->v[0], fwd, org );
			VectorMA( org, l->v[1], left, org );
			org[2] += l->v[2];
		}
		trap->R_AddLightToScene( org, l->radius, l->rgb[0], l->rgb[1], l->rgb[2] );
	}
	for ( n = 0; n < dir.numActors; n++ ) {
		dirActor_t			*a = &dir.actors[n];
		const dirEvent_t	*look = NULL;
		float				saberOn = 1.0f, saberT = -10.0f;
		vec3_t				pos, head, at, d, ang;
		float				yaw, wantYaw = 0.0f, wantPitch = 0.0f, k, radius;
		int					anim = -1;

		if ( !a->ghoul2 ) {
			continue;
		}
		for ( i = 0; i < dir.numEvents && dir.events[i].t <= t; i++ ) {
			if ( dir.events[i].actor != n ) {
				continue;
			}
			if ( dir.events[i].type == DE_ANIM ) {
				anim = i;
			} else if ( dir.events[i].type == DE_SABER ) {
				saberOn = dir.events[i].f;
				saberT = dir.events[i].t;
			} else if ( dir.events[i].type == DE_LOOK ) {
				look = dir.events[i].target == DT_NONE ? NULL : &dir.events[i];
			}
		}
		if ( anim != a->animEvent ) {
			// started on the shot's clock, not this frame's, so a seek or a replay lands on the same pose
			const dirEvent_t *e;

			a->animEvent = anim;
			if ( anim >= 0 ) {
				e = &dir.events[anim];
				trap->G2API_SetBoneAnim( a->ghoul2, 0, "model_root", e->first, e->last,
					e->flags | ( e->blend > 0 ? BONE_ANIM_BLEND : 0 ), e->speed,
					dir.paused ? cg.time - (int)( ( dir.pauseT - e->t ) * 1000.0f ) : dir.startTime + (int)( e->t * 1000.0f ),
					-1, e->blend > 0 ? e->blend : -1 );
			}
		}
		if ( !Dir_ActorPose( n, t, pos, &yaw, qtrue ) ) {
			continue;
		}
		if ( !Dir_BonePoint( a->ghoul2, "cranium", pos, yaw, a->scale, head ) ) {
			VectorCopy( pos, head );
			head[2] += 36.0f * a->scale;
		}
		if ( look && Dir_TargetPoint( look, t, at, NULL, qtrue, qtrue ) ) {
			VectorSubtract( at, head, d );
			vectoangles( d, ang );
			wantYaw = AngleSubtract( ang[YAW], yaw );
			wantPitch = AngleNormalize180( ang[PITCH] );
			if ( wantYaw > 65.0f ) wantYaw = 65.0f;
			if ( wantYaw < -65.0f ) wantYaw = -65.0f;
			if ( wantPitch > 25.0f ) wantPitch = 25.0f;
			if ( wantPitch < -55.0f ) wantPitch = -55.0f;
			a->looked = qtrue;
		}
		k = cg.frametime * 0.004f;
		if ( k > 1.0f || dir.paused ) k = 1.0f;		// a held still: the head is already there
		if ( k < 0.0f ) k = 0.0f;
		a->lookYaw += ( wantYaw - a->lookYaw ) * k;
		a->lookPitch += ( wantPitch - a->lookPitch ) * k;

		radius = 72.0f * a->scale;
		VectorCopy( pos, at );
		at[2] += 10.0f * a->scale;
		if ( CG_CullPointAndRadius( at, radius ) ) {
			continue;
		}
		if ( a->looked ) {
			// the neck takes 40% of the turn and the head the rest, over the playing animation
			VectorSet( ang, a->lookPitch * 0.4f, a->lookYaw * 0.4f, 0.0f );
			trap->G2API_SetBoneAngles( a->ghoul2, 0, "cervical", ang, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL, 0, cg.time );
			VectorSet( ang, a->lookPitch * 0.6f, a->lookYaw * 0.6f, 0.0f );
			trap->G2API_SetBoneAngles( a->ghoul2, 0, "cranium", ang, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL, 0, cg.time );
		}
		memset( &ent, 0, sizeof( ent ) );
		ent.reType = RT_MODEL;
		ent.ghoul2 = a->ghoul2;
		ent.customSkin = a->skin;
		ent.radius = radius;
		ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
		VectorCopy( pos, ent.origin );
		VectorCopy( pos, ent.oldorigin );
		VectorCopy( pos, ent.lightingOrigin );
		VectorSet( ent.angles, 0.0f, yaw, 0.0f );
		AnglesToAxis( ent.angles, ent.axis );
		VectorSet( ent.modelScale, a->scale, a->scale, a->scale );
		if ( a->scale != 1.0f ) {
			for ( j = 0; j < 3; j++ ) {
				VectorScale( ent.axis[j], a->scale, ent.axis[j] );
			}
			ent.nonNormalizedAxes = qtrue;
		}
		trap->R_AddRefEntityToScene( &ent );
		if ( a->saber ) {
			// the blade grows or shrinks over 0.3 s from its last "saber on|off"
			mdxaBone_t	m;
			vec3_t		sc, org, bdir;
			float		k2 = ( t - saberT ) / 0.3f;

			if ( k2 > 1.0f ) k2 = 1.0f;
			if ( k2 < 0.0f ) k2 = 0.0f;
			k2 = saberOn > 0.5f ? k2 : 1.0f - k2;
			VectorSet( sc, a->scale, a->scale, a->scale );
			if ( dirDebug > 0 ) {
				qboolean ok = trap->G2API_GetBoltMatrix( a->ghoul2, 1, a->bladeBolt, &m, ent.angles, pos, cg.time, cgs.gameModels, sc );

				dirDebug--;
				BG_GiveMeVectorFromMatrix( &m, ORIGIN, org );
				trap->Print( "director: %s blade: bolt matrix %i, k %.2f, at %.0f %.0f %.0f (body %.0f %.0f %.0f)\n", a->id, ok, k2, org[0], org[1], org[2], pos[0], pos[1], pos[2] );
			}
			if ( k2 > 0.01f && trap->G2API_GetBoltMatrix( a->ghoul2, 1, a->bladeBolt, &m, ent.angles, pos, cg.time, cgs.gameModels, sc ) ) {
				BG_GiveMeVectorFromMatrix( &m, ORIGIN, org );
				BG_GiveMeVectorFromMatrix( &m, NEGATIVE_Y, bdir );
				VectorNormalize( bdir );
				CG_DoSaber( org, bdir, a->saberLen * k2 * a->scale, a->saberLen * a->scale, 3.0f * a->scale, a->saberColor, 0, qtrue );
			}
		}
	}
}

static void Dir_Restart( float from ) {
	int i;

	Dir_ResolveDrops();
	dir.startTime = cg.time - (int)( from * 1000.0f );
	dir.paused = dir.ended = dir.armed = qfalse;
	dir.playing = qtrue;
	trap->Print( "director: %s at %.3f s, game clock %i ms\n", dir.name, from, cg.time );
	if ( from <= 0.0f && dir.startCmd[0] ) {
		trap->SendConsoleCommand( va( "%s\n", dir.startCmd ) );
	}
	for ( i = 0; i < dir.numActors; i++ ) {
		dir.actors[i].animEvent = -2;	// restart whatever should be playing at the new time
		dir.actors[i].lookYaw = dir.actors[i].lookPitch = 0.0f;
	}
	trap->Cvar_Set( "director_done", "0" );
}

static void Dir_Append( const char *line ) {
	fileHandle_t f;

	if ( trap->FS_Open( "director/keys.txt", &f, FS_APPEND ) < 0 || !f ) {
		trap->Print( "director: could not write director/keys.txt\n" );
		return;
	}
	trap->FS_Write( line, strlen( line ), f );
	trap->FS_Close( f );
	trap->Print( "%s", line );
}

// ---- "director check": what a contact sheet would show, found by tracing instead of looking ----

static const vec3_t dirBodyMins = { -15, -15, DEFAULT_MINS_2 }, dirBodyMaxs = { 15, 15, DEFAULT_MAXS_2 };

// a game point in the file's own units (its last "frame"), for a report the shot's author can paste back
static const char *Dir_Src( const vec3_t v ) {
	if ( dirFrameLast.on && dirFrameLast.scale != 0.0f ) {
		return va( "%.2f %.2f %.2f", v[0] / dirFrameLast.scale + dirFrameLast.org[0], v[1] / dirFrameLast.scale + dirFrameLast.org[1],
			v[2] / dirFrameLast.scale + dirFrameLast.org[2] );
	}
	return va( "%.1f %.1f %.1f", v[0], v[1], v[2] );
}

static float Dir_SrcZ( float z ) {
	return ( dirFrameLast.on && dirFrameLast.scale != 0.0f ) ? z / dirFrameLast.scale + dirFrameLast.org[2] : z;
}

// the player-origin height of every floor someone could stand on at this x y, from 1024 above the point down, top first
static int Dir_Floors( const vec3_t at, float *z, int max ) {
	trace_t	tr;
	vec3_t	start, end;
	int		n = 0, guard = 0;

	VectorCopy( at, start );
	VectorCopy( at, end );
	start[2] += 1024.0f;
	end[2] -= 4096.0f;
	while ( n < max && guard++ < 400 && start[2] > end[2] ) {
		CG_Trace( &tr, start, dirBodyMins, dirBodyMaxs, end, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid ) {
			start[2] -= 16.0f;
			continue;
		}
		if ( tr.fraction >= 1.0f ) {
			break;
		}
		if ( !n || z[n - 1] - tr.endpos[2] > 8.0f ) {
			z[n++] = tr.endpos[2];
		}
		start[2] = tr.endpos[2] - 16.0f;
	}
	return n;
}

#define DIR_CHK_KINDS 4

static void Dir_Check( float step ) {
	static const vec3_t	camMins = { -6, -6, -6 }, camMaxs = { 6, 6, 6 };
	static const char	*kind[DIR_CHK_KINDS] = { "is inside or within 6 u of a wall", "passes through a surface",
		"cannot see what it is aimed at", "jumps without a cut (over 3,000 u/s; a body it is rel to turned or moved?)" };
	float		since[DIR_CHK_KINDS], prevT = 0.0f, t;
	vec3_t		where[DIR_CHK_KINDS], prev, org, ang, at;
	qboolean	open[DIR_CHK_KINDS] = { qfalse, qfalse, qfalse, qfalse }, havePrev = qfalse;
	int			i, j, k, problems = 0;
	trace_t		tr;

	trap->Print( "director check: %s, %.2f s, positions as written in the file (%s)\n", dir.name, dir.length,
		dirFrameLast.on ? "its frame" : "game units" );

	// actors: where each pos and move lands, and every floor under it
	for ( i = 0; i < dir.numEvents; i++ ) {
		const dirEvent_t	*e = &dir.events[i];
		float				z[8], nearest = 99999.0f;
		char				floors[256];
		vec3_t				p;
		int					n;

		if ( e->type != DE_POS && e->type != DE_MOVE ) {
			continue;
		}
		n = Dir_Floors( e->raw, z, 8 );
		floors[0] = 0;
		for ( j = 0; j < n; j++ ) {
			Q_strcat( floors, sizeof( floors ), va( "%s%.2f%s", j ? ", " : "", Dir_SrcZ( z[j] ), fabs( z[j] - e->v[2] ) < 1.0f ? "*" : "" ) );
			if ( fabs( z[j] - e->v[2] ) < fabs( nearest ) ) {
				nearest = z[j] - e->v[2];
			}
		}
		trap->Print( "  line %i: %s %s at %s, floors z: %s\n", e->line, dir.actors[e->actor].id, e->type == DE_POS ? "pos" : "move to",
			Dir_Src( e->v ), n ? floors : "none" );
		if ( !n ) {
			trap->Print( S_COLOR_YELLOW "    ! no ground here: a hole, or the map's collision is not loaded this far from the player\n" );
			problems++;
			continue;
		}
		if ( fabs( nearest ) > 2.0f ) {
			trap->Print( S_COLOR_YELLOW "    ! %s the nearest floor by %.0f u (%.2f in the file's units)%s\n", nearest < 0 ? "floats above" : "sunk below",
				fabs( nearest ), fabs( nearest ) / ( dirFrameLast.on ? dirFrameLast.scale : 1.0f ), e->dropped ? "" : "; add drop" );
			problems++;
		}
		VectorCopy( e->v, p );
		p[2] += 1.0f;
		CG_Trace( &tr, p, dirBodyMins, dirBodyMaxs, p, ENTITYNUM_NONE, MASK_PLAYERSOLID );
		if ( tr.startsolid ) {
			trap->Print( S_COLOR_YELLOW "    ! the body is inside something solid here\n" );
			problems++;
		}
		// someone else nearby standing a storey higher or lower is usually a canopy or a roof taken for the street
		for ( j = 0; j < i; j++ ) {
			const dirEvent_t	*o = &dir.events[j];
			float				dx, dy;

			if ( ( o->type != DE_POS && o->type != DE_MOVE ) || o->actor == e->actor ) {
				continue;
			}
			dx = o->v[0] - e->v[0];
			dy = o->v[1] - e->v[1];
			if ( dx * dx + dy * dy < 600.0f * 600.0f && fabs( o->v[2] - e->v[2] ) > 48.0f ) {
				trap->Print( S_COLOR_YELLOW "    ! %.0f u %s %s (line %i), who is %.0f u away: different floors?\n", fabs( o->v[2] - e->v[2] ),
					e->v[2] > o->v[2] ? "above" : "below", dir.actors[o->actor].id, o->line, sqrt( dx * dx + dy * dy ) );
				problems++;
			}
		}
		if ( e->type == DE_MOVE ) {
			vec3_t	from, a, b;
			float	yaw, dist;

			if ( Dir_ActorPose( e->actor, e->t, from, &yaw, qfalse ) ) {
				VectorCopy( from, a );
				VectorCopy( e->v, b );
				a[2] += 20.0f;		// over kerbs and steps
				b[2] += 20.0f;
				dist = Distance( from, e->v );
				CG_Trace( &tr, a, dirBodyMins, dirBodyMaxs, b, ENTITYNUM_NONE, MASK_PLAYERSOLID );
				trap->Print( "    walks %.0f u in %.1f s = %.0f u/s (a walk is about 55, a run about 225), height change %.0f u\n", dist, e->f,
					dist / e->f, e->v[2] - from[2] );
				if ( tr.startsolid || tr.fraction < 1.0f ) {
					trap->Print( S_COLOR_YELLOW "    ! the way is blocked %.0f u along\n", tr.startsolid ? 0.0f : dist * tr.fraction );
					problems++;
				}
			}
		}
	}

	// heads: how far each look asks the head to turn (bones as posed now, so within a few degrees)
	for ( i = 0; i < dir.numEvents; i++ ) {
		const dirEvent_t	*e = &dir.events[i];
		const dirActor_t	*a;
		vec3_t				pos, head, d;
		float				yaw, wy, wp;

		if ( e->type != DE_LOOK || e->target == DT_NONE || e->target == DT_CAM ) {
			continue;
		}
		a = &dir.actors[e->actor];
		if ( !Dir_ActorPose( e->actor, e->t, pos, &yaw, qtrue ) || !Dir_TargetPoint( e, e->t, at, NULL, qtrue, qtrue ) ) {
			continue;
		}
		if ( !Dir_BonePoint( a->ghoul2, "cranium", pos, yaw, a->scale, head ) ) {
			VectorCopy( pos, head );
			head[2] += 36.0f * a->scale;
		}
		VectorSubtract( at, head, d );
		vectoangles( d, ang );
		wy = AngleSubtract( ang[YAW], yaw );
		wp = AngleNormalize180( ang[PITCH] );
		trap->Print( "  line %i: %s look at %.2f s: head %.0f u up, target %.0f u away and %.0f u %s; turn %.0f, tilt %.0f (%s)\n", e->line, a->id,
			e->t, head[2] - pos[2], sqrt( d[0] * d[0] + d[1] * d[1] ), fabs( d[2] ), d[2] > 0 ? "higher" : "lower", wy, wp, wp < 0 ? "up" : "down" );
		if ( fabs( wy ) > 65.0f || wp > 25.0f || wp < -55.0f ) {
			trap->Print( S_COLOR_YELLOW "    ! past what the neck allows (65 to a side, 55 up, 25 down): turn the body with face\n" );
			problems++;
		}
	}

	// camera: every step of the path against the map
	if ( dir.numKeys ) {
		for ( t = 0.0f; ; t += step ) {
			qboolean	bad[DIR_CHK_KINDS] = { qfalse, qfalse, qfalse, qfalse }, looking, last = qfalse, cut = qfalse;
			float		fov;

			if ( t >= dir.length ) {
				t = dir.length;
				last = qtrue;
			}
			looking = Dir_ViewAt( t, org, ang, &fov, at );
			CG_Trace( &tr, org, camMins, camMaxs, org, ENTITYNUM_NONE, MASK_SOLID );
			bad[0] = ( tr.startsolid || ( CG_PointContents( org, ENTITYNUM_NONE ) & CONTENTS_SOLID ) ) ? qtrue : qfalse;
			for ( k = 0; k < dir.numKeys; k++ ) {
				if ( dir.keys[k].cut && dir.keys[k].t > prevT && dir.keys[k].t <= t ) {
					cut = qtrue;
				}
			}
			if ( havePrev && !cut ) {
				CG_Trace( &tr, prev, NULL, NULL, org, ENTITYNUM_NONE, MASK_SOLID );
				bad[1] = ( tr.fraction < 1.0f && !tr.startsolid ) ? qtrue : qfalse;
				bad[3] = ( t > prevT && Distance( prev, org ) / ( t - prevT ) > 3000.0f ) ? qtrue : qfalse;
			}
			if ( looking ) {
				CG_Trace( &tr, org, NULL, NULL, at, ENTITYNUM_NONE, MASK_SOLID );
				bad[2] = ( tr.fraction < 0.97f ) ? qtrue : qfalse;
			}
			for ( k = 0; k < DIR_CHK_KINDS; k++ ) {
				if ( bad[k] && !open[k] ) {
					open[k] = qtrue;
					since[k] = t;
					VectorCopy( org, where[k] );
				}
				if ( open[k] && ( !bad[k] || last ) ) {
					open[k] = qfalse;
					trap->Print( S_COLOR_YELLOW "  ! camera %s from %.2f to %.2f s (first at %s)\n", kind[k], since[k], bad[k] ? t : prevT, Dir_Src( where[k] ) );
					problems++;
				}
			}
			VectorCopy( org, prev );
			prevT = t;
			havePrev = qtrue;
			if ( last ) {
				break;
			}
		}
	}
	trap->Print( "director check: %i problem%s\n", problems, problems == 1 ? "" : "s" );
}

void CG_Director_f( void ) {
	const char *sub = CG_Argv( 1 );

	if ( !Q_stricmp( sub, "play" ) && trap->Cmd_Argc() >= 3 ) {
		// a detached camera sees past walls: a demo, or a server with cheats on
		if ( !cg.demoPlayback && atoi( Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_cheats" ) ) == 0 ) {
			trap->Print( "director: needs sv_cheats 1 or a demo\n" );
			return;
		}
		float from = trap->Cmd_Argc() >= 4 ? atof( CG_Argv( 3 ) ) : 0.0f;

		if ( Dir_Load( CG_Argv( 2 ) ) ) {
			Dir_Restart( from );
		}
	} else if ( !Q_stricmp( sub, "arm" ) && trap->Cmd_Argc() >= 4 ) {
		int clock = atoi( CG_Argv( 3 ) );

		if ( !cg.demoPlayback && atoi( Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_cheats" ) ) == 0 ) {
			trap->Print( "director: needs sv_cheats 1 or a demo\n" );
			return;
		}
		if ( Dir_Load( CG_Argv( 2 ) ) ) {
			// the first pose shows (camera and actors) until the game clock gets there
			dir.armed = qtrue;
			dir.playing = qtrue;
			dir.armTime = clock;
		}
	} else if ( !Q_stricmp( sub, "show" ) && trap->Cmd_Argc() >= 4 ) {
		float at = atof( CG_Argv( 3 ) );
		int i;

		if ( !cg.demoPlayback && atoi( Info_ValueForKey( CG_ConfigString( CS_SYSTEMINFO ), "sv_cheats" ) ) == 0 ) {
			trap->Print( "director: needs sv_cheats 1 or a demo\n" );
			return;
		}
		// the same file again: keep its models, only move the time
		if ( ( dir.loaded && !Q_stricmp( dir.name, CG_Argv( 2 ) ) ) || Dir_Load( CG_Argv( 2 ) ) ) {
			Dir_ResolveDrops();
			dir.startTime = cg.time - (int)( at * 1000.0f );
			dir.pauseT = at;
			dir.paused = dir.playing = qtrue;
			dir.ended = dir.armed = qfalse;
			for ( i = 0; i < dir.numActors; i++ ) {
				dir.actors[i].animEvent = -2;
			}
		}
	} else if ( !Q_stricmp( sub, "check" ) && trap->Cmd_Argc() >= 3 ) {
		float step = trap->Cmd_Argc() >= 4 ? atof( CG_Argv( 3 ) ) : 0.05f;
		char name[MAX_QPATH];

		Q_strncpyz( name, CG_Argv( 2 ), sizeof( name ) );
		if ( Dir_Load( name ) ) {
			Dir_ResolveDrops();
			Dir_Check( step > 0.005f ? step : 0.05f );
		}
	} else if ( !Q_stricmp( sub, "floors" ) && trap->Cmd_Argc() >= 4 ) {
		// every floor at an x y (game units), near the player's height: for picking a z before writing a pos line
		float	z[8];
		vec3_t	p;
		int		n, i;

		VectorSet( p, atof( CG_Argv( 2 ) ), 0.0f, cg.predictedPlayerState.origin[2] );
		p[1] = atof( CG_Argv( 3 ) );
		if ( trap->Cmd_Argc() >= 5 ) {
			p[2] = atof( CG_Argv( 4 ) );
		}
		n = Dir_Floors( p, z, 8 );
		trap->Print( "director: %i floor%s at %.1f %.1f:", n, n == 1 ? "" : "s", p[0], p[1] );
		for ( i = 0; i < n; i++ ) {
			trap->Print( " %.1f", z[i] );
		}
		trap->Print( "\n" );
	} else if ( !Q_stricmp( sub, "radar" ) && trap->Cmd_Argc() >= 5 ) {
		// how far a camera can back away from a point (game units) before it meets a wall: straight up, then every
		// 15 degrees of yaw from 0, level; for finding where there is room for a shot
		static const vec3_t	mins = { -6, -6, -6 }, maxs = { 6, 6, 6 };
		vec3_t	p, end;
		trace_t	tr;
		char	line[512];
		int		i;

		VectorSet( p, atof( CG_Argv( 2 ) ), 0.0f, 0.0f );
		p[1] = atof( CG_Argv( 3 ) );
		p[2] = atof( CG_Argv( 4 ) );
		if ( !Q_stricmp( CG_Argv( 2 ), "here" ) ) {
			// "radar here 0 0": from the player's own eye height
			VectorCopy( cg.predictedPlayerState.origin, p );
			p[2] += 16.0f;
		} else if ( trap->Cmd_Argc() >= 6 ) {
			// "drop": from eye height above the floor under the point (printed as a player origin)
			Dir_Drop( p, 0 );
			trap->Print( "director: radar floor %.1f\n", p[2] );
			p[2] += 40.0f;
		}
		VectorCopy( p, end );
		end[2] += 8000.0f;
		CG_Trace( &tr, p, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
		Com_sprintf( line, sizeof( line ), "director: radar %.1f %.1f %.1f: up %.0f%s, around", p[0], p[1], p[2], tr.fraction * 8000.0f,
			tr.startsolid ? " (in solid)" : "" );
		for ( i = 0; i < 24; i++ ) {
			float a = i * 15.0f * ( M_PI / 180.0f );

			VectorSet( end, p[0] + cos( a ) * 6000.0f, p[1] + sin( a ) * 6000.0f, p[2] );
			CG_Trace( &tr, p, mins, maxs, end, ENTITYNUM_NONE, MASK_SOLID );
			Q_strcat( line, sizeof( line ), va( " %.0f", tr.fraction * 6000.0f ) );
		}
		trap->Print( "%s\n", line );
	} else if ( !Q_stricmp( sub, "stop" ) ) {
		CG_Director_Shutdown();
	} else if ( !Q_stricmp( sub, "seek" ) && trap->Cmd_Argc() >= 3 && dir.loaded ) {
		qboolean paused = dir.paused;

		Dir_Restart( atof( CG_Argv( 2 ) ) );
		if ( paused ) {
			dir.pauseT = atof( CG_Argv( 2 ) );
			dir.paused = qtrue;
		}
	} else if ( !Q_stricmp( sub, "pause" ) && dir.loaded ) {
		if ( dir.paused ) {
			Dir_Restart( dir.pauseT );
		} else {
			dir.pauseT = Dir_Time();
			dir.paused = qtrue;
			dir.playing = qtrue;
		}
	} else if ( !Q_stricmp( sub, "time" ) ) {
		trap->Print( "director: game clock %i ms%s\n", cg.time, dir.loaded ? va( ", %s at %.3f of %.3f s%s", dir.name,
			Dir_Time(), dir.length,  dir.armed ? " (armed)" : dir.paused ? " (paused)" : dir.ended ? " (ended)" : "" ) : "" );
	} else if ( !Q_stricmp( sub, "key" ) ) {
		float t = trap->Cmd_Argc() >= 3 ? atof( CG_Argv( 2 ) ) : ( dir.loaded ? Dir_Time() : 0.0f );

		Dir_Append( va( "cam %.2f %.1f %.1f %.1f %.2f %.2f %.2f %.1f\n", t, cg.refdef.vieworg[0], cg.refdef.vieworg[1],
			cg.refdef.vieworg[2], cg.refdef.viewangles[YAW], cg.refdef.viewangles[PITCH], cg.refdef.viewangles[ROLL],
			cg_fov.value ) );
	} else if ( !Q_stricmp( sub, "here" ) && trap->Cmd_Argc() >= 4 ) {
		const float	*o = cg.predictedPlayerState.origin;
		char		id[32], model[MAX_QPATH], anim[64];

		// CG_Argv hands back one shared buffer: copy each word before asking for the next
		Q_strncpyz( id, CG_Argv( 2 ), sizeof( id ) );
		Q_strncpyz( model, CG_Argv( 3 ), sizeof( model ) );
		Q_strncpyz( anim, trap->Cmd_Argc() >= 5 ? CG_Argv( 4 ) : "BOTH_STAND1", sizeof( anim ) );
		Dir_Append( va( "actor %s %s\nat 0 %s pos %.1f %.1f %.1f %.1f\nat 0 %s anim %s\n", id, model, id, o[0], o[1], o[2],
			cg.predictedPlayerState.viewangles[YAW], id, anim ) );
	} else {
		trap->Print( "usage: director play <name> [from] | arm <name> <clock ms> | show <name> <t> | check <name> [step] | floors <x> <y> [z] | radar <x> <y> <z> | stop | seek <t> | pause | time | key [t] | here <id> <model> [ANIM]\n"
			"  shot files are director/<name>.txt; key and here append to director/keys.txt\n" );
	}
}
