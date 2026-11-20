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
The character picker screen (docs/character-loading-plan.md, M4c): the roster (ui_roster.c) down the left, the
selected outfit live on a stage, its outfits under it, details and Apply on the right.

The screen is ui/jamp/xen_character.menu (menu "xen_character", from a pack; loaded beside every menu set when it is
there) with four ownerdraws:
	UI_PICKER_ROSTER	400		one row per character, the selected one highlighted
	UI_PICKER_STAGE		401		the selected outfit's model, or its icon until the model is in
	UI_PICKER_VARIANTS	402		< previous . selected . next >, and "n / count"
	UI_PICKER_DETAILS	403		name, source, outfit, team colour chips, the stage's state
and menu scripts (uiScript): pickerOpen and pickerClose (the menu's onOpen and onClose), pickerApply, pickerBack,
pickerCharacter <step>, pickerVariant <step>, pickerTeam <0|1|2>, pickerFace. `ui_characters` opens it.

Keys (UI_PickerMenuKey, ahead of the focused item): Up/Down, W/S and the wheel the character; Left/Right and A/D the
outfit; 1/2/3 the team colour; F the face view; Enter applies (unless a button has the focus: then it is that
button's); clicks on a row, the strip's halves or a chip select; Esc leaves (UI_KeyEvent hands it here first, since a
non-fullscreen menu's Esc would otherwise close every menu and leave the key catcher set).

The menu is full screen with an opaque backdrop when disconnected (the client reopens the main menu on any frame
without a full-screen menu) and a dimmed overlay over the game when connected: UI_Characters_f sets that per open.

Loading: the stage asks for the selected outfit once the cursor has rested on it for PICKER_DWELL_MS. With the
engine's warm-up (cl_uiWarmApi) the renderer loads it in steps (WARMMODEL_OTHER_PLAYER: no texture pin, the
asked skin only) and the stage builds its instance once that is ready; without it the stage registers the model at
once, at most PICKER_LIVE_CAP distinct ones per UI session (nothing frees a model mid-map), and shows the icon past
that. Apply sets the model cvar as the grid does; in game the engine then holds the userinfo until the model is
loaded (cl_modelWarm).
*/

#include "ui_local.h"
#include "game/anims.h"
#include "ghoul2/G2.h"


#define PICKER_MENU			"xen_character"
#define PICKER_MENU_FILE	"ui/jamp/xen_character.menu"

#define UI_PICKER_ROSTER	400
#define UI_PICKER_STAGE		401
#define UI_PICKER_VARIANTS	402
#define UI_PICKER_DETAILS	403

#define PICKER_DWELL_MS		500
#define PICKER_LIVE_CAP		4		// without the warm-up: distinct live models a UI session may register
#define PICKER_ROW_H		16.0f
#define PICKER_EDGE_ROWS	3		// roster rows that fade at an edge with more past it; the selection stays clear of them
#define PICKER_FOV_X		30.0f
#define PICKER_FEET_Z		-24.0f	// a player model's feet below its origin (DEFAULT_MINS_2)
#define PICKER_HEIGHT		70.0f	// stage height without character.cfg framing
#define PICKER_YAW			155.0f	// three-quarter view, facing the camera
#define PICKER_TWEEN_MS		320		// a framing change (design 3.3: outcubic)
#define PICKER_BLEND_MS		200		// an animation change
// the stage instance lives on the UI's clock: it is animated, queried and (RDF_STAGE) drawn at uiDC.realTime, where the
// renderer takes a time of ours (G2_TIME_EXACT); elsewhere all of it is at the shared Ghoul2 clock, as stock
#define PICKER_NOW			( uiInfo.uiDC.realTime | G2_TIME_EXACT )

typedef enum {
	PICKER_SHOWN,		// the model is on the stage
	PICKER_RESTING,		// waiting for the cursor to rest
	PICKER_LOADING,		// the warm-up runs
	PICKER_CAPPED,		// no more live models this run (no warm-up)
	PICKER_FAILED		// the model would not load
} pickerState_t;

typedef struct picker_s {
	int				character;		// uiRoster.characters index
	int				variant;		// within the character
	rosterTeam_t	team;
	int				top;			// first roster row drawn
	qboolean		face;
	int				changed;		// realTime of the last selection change

	void			*ghoul2;		// the stage's instance
	int				liveVariant;	// what it shows (uiRoster.variants index, -1 none)
	rosterTeam_t	liveTeam;
	qhandle_t		liveSkin;
	int				warmHandle;
	char			warmFor[MAX_QPATH];
	char			loaded[PICKER_LIVE_CAP][MAX_QPATH];
	int				numLoaded;
	pickerState_t	state;

	// the player screen's stage (UI_PickerStage*): what it asks for, and what the instance has
	const animation_t	*anims;		// the instance's skeleton's animation table (bgAllAnims or pickerAnims; NULL none)
	int				baseAnim;		// looped (-1: the outfit's idle)
	int				gesture, gestureEnds;	// played once over the base
	int				liveAnim;
	rosterTeam_t	preview;		// a team skin shown instead of the draft's (ROSTER_NUM_TEAMS: none)
	char			sabers[2][MAX_QPATH], liveSabers[2][MAX_QPATH];
	saber_colors_t	colors[2];
	qboolean		lit;
	int				animStart;		// realTime liveAnim started
	int				hands[2];		// the instance's *r_hand / *l_hand bolts (-1 none)

	rectDef_t		roster, strip, chips[ROSTER_NUM_TEAMS];
	int				rows;
	qhandle_t		icons[ROSTER_MAX_VARIANTS];
	qboolean		iconTried[ROSTER_MAX_VARIANTS];
} picker_t;

static picker_t picker = { 0, 0, ROSTER_TEAM_DEFAULT, 0, qfalse, 0, NULL, -1 };

// the stage's framing (the player screen sets it per section): zoom 1 fits the whole figure; center is the height
// fraction the view centres on; yaw turns the figure (180 faces the camera; above it the figure turns to screen
// right). A change tweens from where the framing is; the first after the picker opens snaps. panel draws the
// stage's dark backing.
typedef struct { float zoom, center, yaw; } pickerFrame_t;
static pickerFrame_t pickerFrom = { 1.0f, 0.5f, PICKER_YAW }, pickerTo = { 1.0f, 0.5f, PICKER_YAW };
static int pickerFrameStart = -PICKER_TWEEN_MS;
static qboolean pickerPanelOn = qtrue, pickerFramed = qfalse;
static qboolean pickerCovers = qfalse;		// UI_PickerStageCovers

// the line the figure keeps left of (the player screen's menu runs down a sheared edge): its x at the top and the
// bottom of the 640x480 screen. The stage slides the figure left, then pulls back, until the hands and the blades clear
// it all through the animation: a pose's reach is twelve samples along its loop (or its gesture), read ahead when it
// first plays (Picker_FitReach) and kept, so the fit holds still while the stance moves and a pose seen before is
// fitted at once.
#define PICKER_FIT_POINTS	( 2 * 2 + 2 * MAX_BLADES * 2 )
#define PICKER_FIT_SAMPLES	12
#define PICKER_FIT_POSES	16
typedef struct {
	int			variant;			// the outfit (uiRoster.variants), the animation playing and the hilts lit or not
	int			anim;
	qboolean	lit;
	char		sabers[2][MAX_QPATH];
	int			used;				// realTime last on the stage (the oldest makes way)
	int			filled;				// the samples taken, a bit each
	int			n[PICKER_FIT_SAMPLES];
	vec3_t		pts[PICKER_FIT_SAMPLES][PICKER_FIT_POINTS];	// segments in the model's own frame (no yaw, at the origin)
} pickerReach_t;
static pickerReach_t pickerReach[PICKER_FIT_POSES];
static struct {
	qboolean		on;
	float			xTop, xBottom;
	float			shift, zoom;	// the fit drawn: world units to screen left, a span multiplier (1: none)
	int				time, snapUntil;	// till snapUntil the fit is drawn as solved (the screen opens fitted)
	pickerReach_t	*pose;
} pickerFit;

static vec4_t pickerDim = { 0.6f, 0.6f, 0.6f, 1.0f };
static vec4_t pickerPanel = { 0.0f, 0.0f, 0.0f, 0.55f };
static vec4_t pickerHighlight = { 0.25f, 0.45f, 0.75f, 0.7f };
static vec4_t pickerChipColor[ROSTER_NUM_TEAMS] = { { 0.75f, 0.75f, 0.75f, 1.0f }, { 0.85f, 0.2f, 0.2f, 1.0f }, { 0.25f, 0.4f, 0.9f, 1.0f } };

/* ------------------------------------------------------------------ selection */

static qboolean Picker_Valid( void ) {
	return (qboolean)( uiRoster.numCharacters > 0 && picker.character >= 0 && picker.character < uiRoster.numCharacters );
}

// the selected uiRoster.variants index, or -1
static int Picker_Variant( void ) {
	const rosterCharacter_t *c;

	if ( !Picker_Valid() ) {
		return -1;
	}
	c = &uiRoster.characters[picker.character];
	if ( picker.variant < 0 || picker.variant >= c->numVariants ) {
		return -1;
	}
	return c->firstVariant + picker.variant;
}

static void Picker_Select( int character, int variant ) {
	const int n = uiRoster.numCharacters;
	int count;

	if ( n <= 0 ) {
		return;
	}
	character = ( ( character % n ) + n ) % n;
	count = uiRoster.characters[character].numVariants;
	variant = count > 0 ? ( ( variant % count ) + count ) % count : 0;
	if ( character != picker.character || variant != picker.variant ) {
		picker.changed = uiInfo.uiDC.realTime;
	}
	picker.character = character;
	picker.variant = variant;
	if ( picker.team != ROSTER_TEAM_DEFAULT && count > 0
		&& !uiRoster.variants[uiRoster.characters[character].firstVariant + variant].skin[picker.team][0] ) {
		picker.team = ROSTER_TEAM_DEFAULT;	// this outfit has no such colour
	}
}

// the selection from the model cvar ("folder/skin"; a team skin selects that colour)
static void Picker_FromCvar( void ) {
	char model[MAX_QPATH];
	int v, t, c;

	trap->Cvar_VariableStringBuffer( "model", model, sizeof( model ) );
	v = UI_RosterFindModel( &uiRoster, model );
	picker.team = ROSTER_TEAM_DEFAULT;
	if ( v < 0 ) {
		Picker_Select( 0, 0 );
		return;
	}
	for ( t = ROSTER_TEAM_RED; t < ROSTER_NUM_TEAMS; t++ ) {
		const char *slash = strchr( model, '/' );
		if ( slash && uiRoster.variants[v].skin[t][0] && !Q_stricmp( slash + 1, uiRoster.variants[v].skin[t] ) ) {
			picker.team = (rosterTeam_t)t;
		}
	}
	c = uiRoster.variants[v].character;
	Picker_Select( c, v - uiRoster.characters[c].firstVariant );
}

static qhandle_t Picker_Icon( int v ) {
	if ( v < 0 || v >= uiRoster.numVariants ) {
		return 0;
	}
	if ( !picker.iconTried[v] ) {
		picker.iconTried[v] = qtrue;
		picker.icons[v] = trap->R_RegisterShaderNoMip( va( "models/players/%s/icon_%s", uiRoster.variants[v].folder,
			uiRoster.variants[v].skin[ROSTER_TEAM_DEFAULT] ) );
	}
	return picker.icons[v];
}

// the character's source as the details show it (character.cfg sourcename; ui_roster.c)
static const char *Picker_SourceName( const rosterCharacter_t *c ) {
	if ( c->sourceIndex < 0 || c->sourceIndex >= uiRoster.numSources ) return "Other";
	return uiRoster.sources[c->sourceIndex].name;
}

/* ------------------------------------------------------------------ the stage's model */

// the team skin the stage shows: the player screen's preview when the outfit has it, else the draft's
static rosterTeam_t Picker_ShownTeam( void ) {
	const int v = Picker_Variant();
	if ( picker.preview < ROSTER_NUM_TEAMS && v >= 0
		&& ( picker.preview == ROSTER_TEAM_DEFAULT || uiRoster.variants[v].skin[picker.preview][0] ) ) {
		return picker.preview;
	}
	return picker.team;
}

static qboolean Picker_Live( int v ) {
	return (qboolean)( picker.ghoul2 && v >= 0 && picker.liveVariant == v && picker.liveTeam == Picker_ShownTeam() );
}

static void Picker_FreeModel( void ) {
	if ( picker.ghoul2 ) {
		UI_ClearG2Pointer( picker.ghoul2 );
		trap->G2API_CleanGhoul2Models( &picker.ghoul2 );
		picker.ghoul2 = NULL;
	}
	picker.liveVariant = -1;
	picker.anims = NULL;
	picker.liveAnim = -1;
	picker.liveSabers[0][0] = picker.liveSabers[1][0] = 0;
	picker.hands[0] = picker.hands[1] = -1;
	pickerFit.pose = NULL;
}

// the idle animation on the instance, from its skeleton's animation.cfg
static animation_t	pickerAnims[MAX_TOTALANIMATIONS];	// the last own-skeleton set parsed (not in bgAllAnims)
static char			pickerAnimsFile[MAX_QPATH];

static void Picker_Animate( const rosterVariant_t *rv ) {
	char gla[MAX_QPATH], cfg[MAX_QPATH];
	char *slash;
	const animation_t *anims;
	int animIndex, anim;

	gla[0] = 0;
	trap->G2API_GetGLAName( picker.ghoul2, 0, gla );
	slash = Q_strrchr( gla, '/' );
	if ( !gla[0] || !slash ) {
		return;
	}
	if ( Com_sprintf( cfg, sizeof( cfg ), "%.*s/animation.cfg", (int)( slash - gla ), gla ) >= (int)sizeof( cfg ) ) {
		return;		// too long a path for MAX_QPATH: no idle
	}
	if ( strstr( cfg, "players/_humanoid/" ) ) {
		animIndex = UI_ParseAnimationFile( cfg, NULL, qfalse );		// the static humanoid set
		if ( animIndex < 0 ) {
			return;
		}
		anims = bgAllAnims[animIndex].anims;
	} else {
		// own skeletons go in one picker set: the 16 UI animation sets would run out after as many outfits
		if ( Q_stricmp( pickerAnimsFile, cfg ) ) {
			pickerAnimsFile[0] = 0;
			if ( !UI_ParseAnimationFileInto( cfg, pickerAnims ) ) {
				return;
			}
			Q_strncpyz( pickerAnimsFile, cfg, sizeof( pickerAnimsFile ) );
		}
		anims = pickerAnims;
	}
	picker.anims = anims;
	anim = rv->idle[0] ? GetIDForString( animTable, rv->idle ) : -1;
	if ( anim < 0 || anim >= MAX_ANIMATIONS ) {
		anim = BOTH_STAND1;
	}
	{
		const animation_t *a = &anims[anim];
		int flags = BONE_ANIM_OVERRIDE_FREEZE;
		if ( a->loopFrames != -1 ) {
			flags |= BONE_ANIM_OVERRIDE_LOOP;
		}
		if ( a->numFrames > 0 && a->frameLerp > 0 ) {
			trap->G2API_SetBoneAnim( picker.ghoul2, 0, "model_root", a->firstFrame, a->firstFrame + a->numFrames, flags,
				50.0f / a->frameLerp, PICKER_NOW, -1, 150 );
			picker.liveAnim = anim;
			picker.animStart = uiInfo.uiDC.realTime;
		}
	}
}

static void Picker_LoadModel( int v, rosterTeam_t team ) {
	const rosterVariant_t *rv = &uiRoster.variants[v];
	const char *skinName = ( team != ROSTER_TEAM_DEFAULT && rv->skin[team][0] ) ? rv->skin[team] : rv->skin[ROSTER_TEAM_DEFAULT];

	if ( picker.ghoul2 && picker.liveVariant == v && picker.state == PICKER_SHOWN ) {
		// a team skin on the shown model: only the skin changes
		picker.liveSkin = trap->R_RegisterSkin( va( "models/players/%s/model_%s.skin", rv->folder, skinName ) );
		if ( picker.liveSkin ) {
			trap->G2API_SetSkin( picker.ghoul2, 0, picker.liveSkin, picker.liveSkin );
		}
		picker.liveTeam = team;
		return;
	}
	Picker_FreeModel();
	picker.liveSkin = trap->R_RegisterSkin( va( "models/players/%s/model_%s.skin", rv->folder, skinName ) );
	if ( trap->G2API_InitGhoul2Model( &picker.ghoul2, va( "models/players/%s/model.glm", rv->folder ), 0, picker.liveSkin, 0, 0, 0 ) < 0
		|| !picker.ghoul2 ) {
		if ( picker.ghoul2 ) {
			trap->G2API_CleanGhoul2Models( &picker.ghoul2 );	// allocated before the model was tried
		}
		picker.ghoul2 = NULL;
		picker.state = PICKER_FAILED;
		picker.liveVariant = v;		// not tried again until the selection changes
		picker.liveTeam = team;
		return;
	}
	UI_InsertG2Pointer( picker.ghoul2 );
	if ( picker.liveSkin ) {
		trap->G2API_SetSkin( picker.ghoul2, 0, picker.liveSkin, picker.liveSkin );
	}
	picker.hands[0] = trap->G2API_AddBolt( picker.ghoul2, 0, "*r_hand" );
	picker.hands[1] = trap->G2API_AddBolt( picker.ghoul2, 0, "*l_hand" );
	Picker_Animate( rv );
	picker.liveVariant = v;
	picker.liveTeam = team;
	picker.state = PICKER_SHOWN;
}

static qboolean Picker_WarmApi( void ) {
	char api[16];

	trap->Cvar_VariableStringBuffer( "cl_uiWarmApi", api, sizeof( api ) );	// the import table is that long
	return (qboolean)( atoi( api ) >= 1 && trap->ext.R_WarmModel && trap->ext.R_WarmState );
}

// every stage frame: the selected outfit's model once it may be shown
static void Picker_UpdateModel( void ) {
	const int v = Picker_Variant();
	char value[MAX_QPATH];
	int i;

	if ( v < 0 ) {
		return;
	}
	if ( picker.liveVariant == v && picker.liveTeam == Picker_ShownTeam() ) {
		return;		// shown, or failed for this selection
	}
	if ( picker.liveVariant == v && picker.ghoul2 && picker.state == PICKER_SHOWN ) {
		Picker_LoadModel( v, Picker_ShownTeam() );	// a team skin only: the skin file, no dwell or warm-up
		return;
	}
	if ( uiInfo.uiDC.realTime - picker.changed < PICKER_DWELL_MS ) {
		picker.state = PICKER_RESTING;
		return;
	}
	UI_RosterModelValue( &uiRoster, v, Picker_ShownTeam(), value, sizeof( value ) );
	if ( Picker_WarmApi() && ( ( picker.warmHandle && !Q_stricmp( picker.warmFor, value ) )
		|| ( picker.warmHandle = trap->ext.R_WarmModel( value, WARMMODEL_OTHER_PLAYER ) ) != 0 ) ) {
		Q_strncpyz( picker.warmFor, value, sizeof( picker.warmFor ) );
		if ( trap->ext.R_WarmState( picker.warmHandle ) == 0 ) {
			picker.state = PICKER_LOADING;
			return;
		}
		picker.warmHandle = 0;	// ready, failed or unknown: the registration below finds what is loaded
	} else {	// no warm-up (or refused): registered at once, a few per session
		for ( i = 0; i < picker.numLoaded && Q_stricmp( picker.loaded[i], value ); i++ ) {
		}
		if ( i == picker.numLoaded ) {
			if ( picker.numLoaded >= PICKER_LIVE_CAP ) {
				picker.state = PICKER_CAPPED;
				return;
			}
			Q_strncpyz( picker.loaded[picker.numLoaded++], value, sizeof( picker.loaded[0] ) );
		}
	}
	Picker_LoadModel( v, Picker_ShownTeam() );
}

static float Picker_OutCubic( float t ) {
	t = 1.0f - t;
	return 1.0f - t * t * t;
}

// the framing now, along its tween
static void Picker_Frame( pickerFrame_t *f ) {
	float t = ( uiInfo.uiDC.realTime - pickerFrameStart ) / (float)PICKER_TWEEN_MS;
	if ( t < 0 ) t = 0;
	if ( t >= 1.0f ) {
		*f = pickerTo;
		return;
	}
	t = Picker_OutCubic( t );
	f->zoom = pickerFrom.zoom + ( pickerTo.zoom - pickerFrom.zoom ) * t;
	f->center = pickerFrom.center + ( pickerTo.center - pickerFrom.center ) * t;
	f->yaw = pickerFrom.yaw + ( pickerTo.yaw - pickerFrom.yaw ) * t;
}

static const animation_t *Picker_Anim( int anim ) {
	const animation_t *a;

	if ( !picker.anims || anim < 0 || anim >= MAX_ANIMATIONS ) {
		return NULL;
	}
	a = &picker.anims[anim];
	return a->numFrames > 0 && a->frameLerp > 0 ? a : NULL;
}

// the stage's animation channel: the gesture while it plays, else the base, blended in
static void Picker_UpdateAnim( void ) {
	const int v = Picker_Variant();
	int want = picker.baseAnim, flags;
	const animation_t *a;

	if ( picker.gesture >= 0 && uiInfo.uiDC.realTime < picker.gestureEnds && Picker_Anim( picker.gesture ) ) {
		want = picker.gesture;
	}
	if ( !Picker_Anim( want ) ) {	// the outfit's idle
		want = v >= 0 && uiRoster.variants[v].idle[0] ? GetIDForString( animTable, uiRoster.variants[v].idle ) : -1;
		if ( !Picker_Anim( want ) ) want = BOTH_STAND1;
	}
	if ( want == picker.liveAnim || ( a = Picker_Anim( want ) ) == NULL ) {
		return;
	}
	flags = BONE_ANIM_OVERRIDE_FREEZE | BONE_ANIM_BLEND;
	if ( a->loopFrames != -1 && want != picker.gesture ) {
		flags = BONE_ANIM_OVERRIDE_LOOP | BONE_ANIM_BLEND;
	}
	trap->G2API_SetBoneAnim( picker.ghoul2, 0, "model_root", a->firstFrame, a->firstFrame + a->numFrames, flags,
		50.0f / a->frameLerp, PICKER_NOW, -1, PICKER_BLEND_MS );
	picker.liveAnim = want;
	picker.animStart = uiInfo.uiDC.realTime;
}

// the hilts in the instance's hands
static void Picker_UpdateSabers( void ) {
	if ( !Q_stricmp( picker.sabers[0], picker.liveSabers[0] ) && !Q_stricmp( picker.sabers[1], picker.liveSabers[1] ) ) {
		return;
	}
	UI_SaberAttachG2( &picker.ghoul2, picker.sabers[0], picker.sabers[1] );
	Q_strncpyz( picker.liveSabers[0], picker.sabers[0], sizeof( picker.liveSabers[0] ) );
	Q_strncpyz( picker.liveSabers[1], picker.sabers[1], sizeof( picker.liveSabers[1] ) );
}

static void Picker_AddBlades( vec3_t origin, vec3_t angles ) {
	int s, b;

	if ( !picker.lit ) {
		return;
	}
	for ( s = 0; s < 2; s++ ) {
		const char *saber = picker.liveSabers[s];
		char typeString[MAX_QPATH] = { 0 };
		saberType_t type;
		int blades;
		if ( !saber[0] || !trap->G2API_HasGhoul2ModelOnIndex( &picker.ghoul2, s + 1 ) ) {
			continue;
		}
		UI_SaberTypeForSaber( saber, typeString );
		type = TranslateSaberType( typeString );
		blades = UI_SaberNumBladesForSaber( saber );
		for ( b = 0; b < blades; b++ ) {
			if ( UI_SaberShouldDrawBlade( saber, b ) ) {
				UI_SaberDrawBladeG2( picker.ghoul2, saber, s + 1, type, origin, angles, b, PICKER_NOW, picker.colors[s] );
			}
		}
	}
}

#define PICKER_FIT_LEFT		0.13f	// the figure's middle stays right of this fraction of the screen; past it the stage pulls back
#define PICKER_FIT_ZOOM		1.6f	// the most it pulls back
#define PICKER_FIT_EASE		90.0f	// ms, towards the solved fit (a new pose or framing; about done as a stance blends in)

// the reach at time, for the figure drawn at origin and angles, as segments (point pairs): the hands (each a point)
// and, lit, every blade from its base to its tip
static int Picker_FitPoints( vec3_t origin, vec3_t angles, int time, vec3_t *pts ) {
	mdxaBone_t m;
	int n = 0, h, s, b;

	for ( h = 0; h < 2; h++ ) {
		if ( picker.hands[h] >= 0
			&& trap->G2API_GetBoltMatrix( picker.ghoul2, 0, picker.hands[h], &m, angles, origin, time, NULL, vec3_origin ) ) {
			BG_GiveMeVectorFromMatrix( &m, ORIGIN, pts[n] );
			VectorCopy( pts[n], pts[n + 1] );
			n += 2;
		}
	}
	if ( !picker.lit ) {
		return n;
	}
	for ( s = 0; s < 2; s++ ) {
		const char *saber = picker.liveSabers[s];
		char typeString[MAX_QPATH] = { 0 };
		int blades;
		saberType_t type;
		if ( !saber[0] || !trap->G2API_HasGhoul2ModelOnIndex( &picker.ghoul2, s + 1 ) ) {
			continue;
		}
		UI_SaberTypeForSaber( saber, typeString );
		type = TranslateSaberType( typeString );
		blades = UI_SaberNumBladesForSaber( saber );
		for ( b = 0; b < blades && b < MAX_BLADES; b++ ) {
			if ( UI_SaberShouldDrawBlade( saber, b )
				&& UI_SaberBladeEndsG2( picker.ghoul2, saber, s + 1, type, origin, angles, b, time, pts[n], pts[n + 1] ) ) {
				n += 2;
			}
		}
	}
	return n;
}

// the pose on the stage, its reach found or made: a new pose is read ahead along its loop (or its gesture) at once,
// where the renderer poses at a time of ours (r_g2TimeExactApi); elsewhere every sample is taken as the animation comes
// to it (after its blend in)
static void Picker_FitReach( void ) {
	const animation_t *a = Picker_Anim( picker.liveAnim );
	const int t = uiInfo.uiDC.realTime - picker.animStart - PICKER_BLEND_MS;
	vec3_t origin = { 0, 0, 0 }, angles = { 0, 0, 0 };
	pickerReach_t *r = NULL;
	int k, span = 0, sample = 0;

	for ( k = 0; k < PICKER_FIT_POSES; k++ ) {
		pickerReach_t *c = &pickerReach[k];
		if ( c->variant == picker.liveVariant + 1 && c->anim == picker.liveAnim && c->lit == picker.lit
			&& !Q_stricmp( c->sabers[0], picker.liveSabers[0] ) && !Q_stricmp( c->sabers[1], picker.liveSabers[1] ) ) {
			r = c;
			break;
		}
		if ( !r || c->used < r->used ) {
			r = c;	// else the oldest makes way
		}
	}
	if ( picker.liveAnim == picker.gesture ) {
		span = picker.gestureEnds - picker.animStart - PICKER_BLEND_MS;
	} else if ( a ) {
		span = a->numFrames * abs( a->frameLerp );
	}
	if ( r->variant != picker.liveVariant + 1 || r->anim != picker.liveAnim || r->lit != picker.lit
		|| Q_stricmp( r->sabers[0], picker.liveSabers[0] ) || Q_stricmp( r->sabers[1], picker.liveSabers[1] ) ) {
		memset( r, 0, sizeof( *r ) );
		r->variant = picker.liveVariant + 1;	// 0: an empty slot
		r->anim = picker.liveAnim;
		r->lit = picker.lit;
		Q_strncpyz( r->sabers[0], picker.liveSabers[0], sizeof( r->sabers[0] ) );
		Q_strncpyz( r->sabers[1], picker.liveSabers[1], sizeof( r->sabers[1] ) );
		if ( span > 0 && trap->Cvar_VariableValue( "r_g2TimeExactApi" ) > 0 ) {
			const int t0 = picker.animStart + PICKER_BLEND_MS;
			const int last = picker.liveAnim == picker.gesture ? PICKER_FIT_SAMPLES - 1 : PICKER_FIT_SAMPLES;	// a gesture to its end
			for ( k = 0; k < PICKER_FIT_SAMPLES; k++ ) {
				r->n[k] = Picker_FitPoints( origin, angles, ( t0 + span * k / last ) | G2_TIME_EXACT, r->pts[k] );
			}
			r->filled = ( 1 << PICKER_FIT_SAMPLES ) - 1;
		}
	}
	r->used = uiInfo.uiDC.realTime;
	pickerFit.pose = r;
	if ( t < 0 ) {
		return;
	}
	if ( span > 0 ) {
		sample = ( picker.liveAnim == picker.gesture ? t : t % span ) * PICKER_FIT_SAMPLES / span;
	}
	if ( sample >= PICKER_FIT_SAMPLES ) {
		sample = PICKER_FIT_SAMPLES - 1;
	}
	if ( !( r->filled & ( 1 << sample ) ) ) {
		r->n[sample] = Picker_FitPoints( origin, angles, PICKER_NOW, r->pts[sample] );
		r->filled |= 1 << sample;
	}
}

// screen x per world unit, times depth, through the stage's view
static float Picker_PerUnit( const refdef_t *rd, const rectDef_t *view ) {
	return ( view->w - 2 ) * 0.5f / tan( rd->fov_x / 360 * M_PI );
}

// the stage camera for a span (the height the view shows) about centerZ: the figure's origin, set aside from the
// view's middle so it stands at homeX on screen, and the view at eye height on the same distance, looking down at the
// framing's centre: head on at the face, from above at the body, never up at the figure
static void Picker_Camera( refdef_t *rd, const rectDef_t *view, float homeX, float span, float centerZ, float faceZ, vec3_t origin ) {
	const float dist = ( span * 0.5f ) / tan( rd->fov_y / 360 * M_PI ), rise = faceZ - centerZ;	// the span fills the view's height
	const float pitch = rise > 0.0f ? asin( rise < dist * 0.6f ? rise / dist : 0.6f ) : 0.0f;
	vec3_t viewAngles;

	VectorSet( origin, dist, ( view->x + view->w * 0.5f - homeX ) * dist / Picker_PerUnit( rd, view ), -centerZ );
	VectorSet( rd->vieworg, dist - dist * cos( pitch ), 0, dist * sin( pitch ) );
	VectorSet( viewAngles, pitch * ( 180.0f / M_PI ), 0, 0 );
	AnglesToAxis( viewAngles, rd->viewaxis );
}

// where a reach point p of the figure at origin, turned by axis, lands on the 640x480 screen: x, y and 1/depth (which
// runs straight along a projected segment, as x and y do)
static void Picker_FitProject( const refdef_t *rd, const rectDef_t *rect, const vec3_t origin, vec3_t axis[3], const float *p, float out[3] ) {
	vec3_t w, d;
	float f;

	VectorMA( origin, p[0], axis[0], w );
	VectorMA( w, p[1], axis[1], w );
	VectorMA( w, p[2], axis[2], w );
	VectorSubtract( w, rd->vieworg, d );
	f = DotProduct( d, rd->viewaxis[0] );
	if ( f < 1.0f ) f = 1.0f;
	out[0] = rect->x + 1 + ( rect->w - 2 ) * 0.5f * ( 1.0f - DotProduct( d, rd->viewaxis[1] ) / ( f * tan( rd->fov_x / 360 * M_PI ) ) );
	out[1] = rect->y + 1 + ( rect->h - 2 ) * 0.5f * ( 1.0f - DotProduct( d, rd->viewaxis[2] ) / ( f * tan( rd->fov_y / 360 * M_PI ) ) );
	out[2] = 1.0f / f;
}

// a reach segment's ends on screen, cut to the screen's height (qfalse: none of it is on screen)
static qboolean Picker_FitSegment( const refdef_t *rd, const rectDef_t *rect, const vec3_t origin, vec3_t axis[3], const float *p0, const float *p1, float a[3], float b[3] ) {
	float t;
	int k;

	Picker_FitProject( rd, rect, origin, axis, p0, a );
	Picker_FitProject( rd, rect, origin, axis, p1, b );
	if ( a[1] > b[1] ) {
		for ( k = 0; k < 3; k++ ) { t = a[k]; a[k] = b[k]; b[k] = t; }
	}
	if ( b[1] < 0 || a[1] > 480 ) {
		return qfalse;
	}
	if ( a[1] < 0 ) {
		t = -a[1] / ( b[1] - a[1] );
		for ( k = 0; k < 3; k++ ) a[k] += ( b[k] - a[k] ) * t;
	}
	if ( b[1] > 480 ) {
		t = ( 480 - a[1] ) / ( b[1] - a[1] );
		for ( k = 0; k < 3; k++ ) b[k] = a[k] + ( b[k] - a[k] ) * t;
	}
	return qtrue;
}

static float Picker_FitLine( float y ) {
	return pickerFit.xTop + ( pickerFit.xBottom - pickerFit.xTop ) * y / 480.0f;
}

// the slide (world units to the view's left) that takes the reach clear of the line with the figure drawn for span,
// and whether it is within how far the figure may slide. Along a segment both x and the line run straight, so its
// ends (cut to the screen) are where it reaches furthest past the line.
static qboolean Picker_FitSlide( refdef_t *rd, const rectDef_t *rect, float homeX, float span, float centerZ, float faceZ, vec3_t axis[3], float *slide ) {
	const float perUnit = Picker_PerUnit( rd, rect );
	const pickerReach_t *r = pickerFit.pose;
	vec3_t origin;
	float need = 0, most;
	int s, i;

	Picker_Camera( rd, rect, homeX, span, centerZ, faceZ, origin );
	most = ( homeX - 640.0f * PICKER_FIT_LEFT ) * origin[0] / perUnit;
	for ( s = 0; r && s < PICKER_FIT_SAMPLES; s++ ) {
		for ( i = 0; ( r->filled & ( 1 << s ) ) && i + 1 < r->n[s]; i += 2 ) {
			float a[3], b[3];
			if ( !Picker_FitSegment( rd, rect, origin, axis, r->pts[s][i], r->pts[s][i + 1], a, b ) ) {
				continue;
			}
			if ( ( a[0] - Picker_FitLine( a[1] ) ) / ( a[2] * perUnit ) > need ) need = ( a[0] - Picker_FitLine( a[1] ) ) / ( a[2] * perUnit );
			if ( ( b[0] - Picker_FitLine( b[1] ) ) / ( b[2] * perUnit ) > need ) need = ( b[0] - Picker_FitLine( b[1] ) ) / ( b[2] * perUnit );
		}
	}
	*slide = need < most ? need : most;
	return (qboolean)( need <= most );
}

// this frame's fit for the framing (span before any pull-back): the least pull-back with which a slide clears the
// reach, eased towards (so a new pose or section glides there)
static void Picker_Fit( refdef_t *rd, const rectDef_t *rect, float homeX, float span, float centerZ, float faceZ, vec3_t axis[3] ) {
	float slide, zoom = 1.0f, a;
	int dt;

	Picker_FitReach();
	if ( !Picker_FitSlide( rd, rect, homeX, span, centerZ, faceZ, axis, &slide ) ) {
		float lo = 1.0f, hi = PICKER_FIT_ZOOM;
		int i;
		for ( i = 0; i < 8; i++ ) {
			const float mid = ( lo + hi ) * 0.5f;
			if ( Picker_FitSlide( rd, rect, homeX, span * mid, centerZ, faceZ, axis, &slide ) ) hi = mid;
			else lo = mid;
		}
		zoom = hi;
		Picker_FitSlide( rd, rect, homeX, span * zoom, centerZ, faceZ, axis, &slide );
	}
	dt = uiInfo.uiDC.realTime - pickerFit.time;
	if ( dt < 0 || dt > 100 ) dt = 100;
	pickerFit.time = uiInfo.uiDC.realTime;
	a = uiInfo.uiDC.realTime < pickerFit.snapUntil ? 1.0f : 1.0f - exp( -dt / PICKER_FIT_EASE );
	pickerFit.shift += ( slide - pickerFit.shift ) * a;
	pickerFit.zoom += ( zoom - pickerFit.zoom ) * a;
}

/* ------------------------------------------------------------------ painting */

// a roster row's opacity: the last PICKER_EDGE_ROWS rows before an edge with more characters past it fade out
// (1 / 0.7 / 0.4 / 0.15 towards the edge), the rest are opaque
static float Picker_RowFade( int row ) {
	const int fromTop = row, fromBottom = picker.rows - 1 - row;
	float fade = 1.0f;

	if ( picker.top > 0 && fromTop < PICKER_EDGE_ROWS ) {
		fade = 0.15f + 0.85f * fromTop / PICKER_EDGE_ROWS;
	}
	if ( picker.top + picker.rows < uiRoster.numCharacters && fromBottom < PICKER_EDGE_ROWS ) {
		const float f = 0.15f + 0.85f * fromBottom / PICKER_EDGE_ROWS;
		if ( f < fade ) fade = f;
	}
	return fade;
}

static void Picker_PaintRoster( const rectDef_t *rect, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	int row;

	picker.roster = *rect;
	picker.rows = (int)( rect->h / PICKER_ROW_H );
	if ( picker.rows < 1 || !Picker_Valid() ) {
		return;
	}
	{	// the selection stays PICKER_EDGE_ROWS from the edges while the list goes on past them
		const int margin = picker.rows > 2 * PICKER_EDGE_ROWS + 1 ? PICKER_EDGE_ROWS : 0;
		if ( picker.character < picker.top + margin ) {
			picker.top = picker.character - margin;
		} else if ( picker.character > picker.top + picker.rows - 1 - margin ) {
			picker.top = picker.character - picker.rows + 1 + margin;
		}
	}
	if ( picker.top > uiRoster.numCharacters - picker.rows ) picker.top = uiRoster.numCharacters - picker.rows;
	if ( picker.top < 0 ) picker.top = 0;
	uiInfo.uiDC.fillRect( rect->x, rect->y, rect->w, rect->h, pickerPanel );
	for ( row = 0; row < picker.rows && picker.top + row < uiRoster.numCharacters; row++ ) {
		const int c = picker.top + row;
		const rosterCharacter_t *ch = &uiRoster.characters[c];
		const float y = rect->y + row * PICKER_ROW_H;
		const qhandle_t icon = Picker_Icon( ch->firstVariant );
		// rows fade towards an edge the list goes on past, so there is visibly more above or below
		const float fade = Picker_RowFade( row );
		vec4_t text, dim, tint = { 1.0f, 1.0f, 1.0f, 1.0f };
		VectorCopy4( c == picker.character ? color : pickerDim, text );
		VectorCopy4( pickerDim, dim );
		text[3] *= fade;
		dim[3] *= fade;
		tint[3] = fade;
		if ( c == picker.character ) {
			uiInfo.uiDC.fillRect( rect->x, y, rect->w, PICKER_ROW_H, pickerHighlight );
		}
		if ( icon ) {
			uiInfo.uiDC.setColor( tint );
			uiInfo.uiDC.drawHandlePic( rect->x + 2, y + 1, PICKER_ROW_H - 2, PICKER_ROW_H - 2, icon );
			uiInfo.uiDC.setColor( NULL );
		}
		Text_Paint( rect->x + PICKER_ROW_H + 4, y + 1, scale, text, ch->name, 0, 0, textStyle, iMenuFont );
		if ( ch->numVariants > 1 ) {
			const char *n = va( "%d", ch->numVariants );
			Text_Paint( rect->x + rect->w - 4 - Text_Width( n, scale, iMenuFont ), y + 1, scale, dim, n, 0, 0, textStyle, iMenuFont );
		}
	}
}

static void Picker_PaintStage( const rectDef_t *rect, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	const int v = Picker_Variant();
	const char *note = NULL;

	if ( pickerPanelOn ) {
		uiInfo.uiDC.fillRect( rect->x, rect->y, rect->w, rect->h, pickerPanel );
	}
	if ( v < 0 ) {
		return;
	}
	Picker_UpdateModel();
	if ( Picker_Live( v ) ) {
		const rosterVariant_t *rv = &uiRoster.variants[v];
		const float height = rv->stageHeight > 0 ? rv->stageHeight : PICKER_HEIGHT;
		const float faceZ = PICKER_FEET_Z + ( rv->faceHeight > 0 ? rv->faceHeight : height - 6.0f );
		pickerFrame_t frame;
		float span, centerZ;
		refdef_t refdef;
		refEntity_t ent;
		vec3_t angles;
		// where the figure stands is the item's middle; the view is the item, or the whole screen when the menu
		// covers it (the stage, the frame's first view, then gets the world's passes: character AO and contact
		// shadows, which work on the full screen)
		const rectDef_t screen = { 0, 0, 640, 480 }, *view = pickerCovers ? &screen : rect;
		const float homeX = rect->x + rect->w * 0.5f;

		Picker_Frame( &frame );
		span = picker.face ? 22.0f : height * 1.08f / frame.zoom;
		centerZ = picker.face ? faceZ : PICKER_FEET_Z + height * frame.center;
		Picker_UpdateSabers();
		Picker_UpdateAnim();

		memset( &refdef, 0, sizeof( refdef ) );
		refdef.rdflags = RDF_NOWORLDMODEL | RDF_STAGE;	// the studio rig where the renderer has it
		AxisClear( refdef.viewaxis );
		// the field of view across the item; a whole-screen view keeps its height
		refdef.fov_x = PICKER_FOV_X;
		refdef.fov_y = atan2( ( rect->h - 2 ) * uiInfo.uiDC.yscale, ( rect->w - 2 ) * uiInfo.uiDC.xscale / tan( refdef.fov_x / 360 * M_PI ) ) * ( 360 / M_PI );
		refdef.x = ( view->x + 1 ) * uiInfo.uiDC.xscale;
		refdef.y = ( view->y + 1 ) * uiInfo.uiDC.yscale;
		refdef.width = ( view->w - 2 ) * uiInfo.uiDC.xscale;
		refdef.height = ( view->h - 2 ) * uiInfo.uiDC.yscale;
		refdef.fov_x = atan2( refdef.width, refdef.height / tan( refdef.fov_y / 360 * M_PI ) ) * ( 360 / M_PI );
		refdef.time = uiInfo.uiDC.realTime;

		memset( &ent, 0, sizeof( ent ) );
		// set aside from the view's middle, the camera sees the figure turned by its bearing: turned back by it, the
		// figure shows the framing's yaw as it would at the middle
		VectorSet( angles, 0, frame.yaw + atan( ( view->x + view->w * 0.5f - homeX ) / Picker_PerUnit( &refdef, view ) ) * ( 180.0f / M_PI ), 0 );
		AnglesToAxis( angles, ent.axis );
		if ( pickerFit.on && !picker.face ) {
			Picker_Fit( &refdef, view, homeX, span, centerZ, faceZ, ent.axis );
			span *= pickerFit.zoom;
		}
		Picker_Camera( &refdef, view, homeX, span, centerZ, faceZ, ent.origin );
		if ( pickerFit.on && !picker.face ) {
			ent.origin[1] += pickerFit.shift;	// +y is the view's left
		}
		ent.ghoul2 = picker.ghoul2;
		ent.radius = 1000;
		ent.customSkin = picker.liveSkin;
		ent.shaderRGBA[0] = ent.shaderRGBA[1] = ent.shaderRGBA[2] = ent.shaderRGBA[3] = 255;
		VectorCopy( ent.origin, ent.oldorigin );
		VectorCopy( ent.origin, ent.lightingOrigin );
		ent.lightingOrigin[2] += faceZ;	// the rig is laid out about the eyes
		ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;

		uiInfo.uiDC.clearScene();
		uiInfo.uiDC.addRefEntityToScene( &ent );
		Picker_AddBlades( ent.origin, angles );
		uiInfo.uiDC.renderScene( &refdef );
		return;
	}
	{	// the icon until the model is in (portraits: M4a, not shot yet)
		const qhandle_t icon = Picker_Icon( v );
		const float s = rect->w < rect->h ? rect->w * 0.5f : rect->h * 0.5f;
		if ( icon ) {
			uiInfo.uiDC.drawHandlePic( rect->x + ( rect->w - s ) * 0.5f, rect->y + ( rect->h - s ) * 0.5f, s, s, icon );
		}
	}
	switch ( picker.state ) {
	case PICKER_LOADING: note = "Loading..."; break;
	case PICKER_CAPPED: note = "Preview limit reached: Apply loads it"; break;
	case PICKER_FAILED: note = "This outfit could not be loaded"; break;
	default: break;
	}
	if ( note ) {
		Text_Paint( rect->x + ( rect->w - Text_Width( note, scale, iMenuFont ) ) * 0.5f, rect->y + rect->h - 24, scale, color, note, 0, 0, textStyle, iMenuFont );
	}
}

static void Picker_PaintVariants( const rectDef_t *rect, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	const rosterCharacter_t *c;
	const char *name, *count;
	float x;

	picker.strip = *rect;
	if ( Picker_Variant() < 0 ) {
		return;
	}
	c = &uiRoster.characters[picker.character];
	uiInfo.uiDC.fillRect( rect->x, rect->y, rect->w, rect->h, pickerPanel );
	name = uiRoster.variants[c->firstVariant + picker.variant].name;
	if ( c->numVariants > 1 ) {
		const char *prev = uiRoster.variants[c->firstVariant + ( picker.variant + c->numVariants - 1 ) % c->numVariants].name;
		const char *next = uiRoster.variants[c->firstVariant + ( picker.variant + 1 ) % c->numVariants].name;
		Text_Paint( rect->x + 6, rect->y + 2, scale, color, "<", 0, 0, textStyle, iMenuFont );
		Text_Paint( rect->x + rect->w - 6 - Text_Width( ">", scale, iMenuFont ), rect->y + 2, scale, color, ">", 0, 0, textStyle, iMenuFont );
		if ( c->numVariants > 2 || Q_stricmp( prev, next ) ) {
			Text_Paint( rect->x + 20, rect->y + 2, scale * 0.8f, pickerDim, prev, 0, 0, textStyle, iMenuFont );
		}
		Text_Paint( rect->x + rect->w - 20 - Text_Width( next, scale * 0.8f, iMenuFont ), rect->y + 2, scale * 0.8f, pickerDim, next, 0, 0, textStyle, iMenuFont );
	}
	x = rect->x + ( rect->w - Text_Width( name, scale, iMenuFont ) ) * 0.5f;
	Text_Paint( x, rect->y + 2, scale, color, name, 0, 0, textStyle, iMenuFont );
	count = va( "%d / %d", picker.variant + 1, c->numVariants );
	Text_Paint( rect->x + ( rect->w - Text_Width( count, scale * 0.7f, iMenuFont ) ) * 0.5f, rect->y + rect->h * 0.55f, scale * 0.7f,
		pickerDim, count, 0, 0, textStyle, iMenuFont );
}

static void Picker_PaintDetails( const rectDef_t *rect, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	static const char *teamNames[ROSTER_NUM_TEAMS] = { "Default", "Red", "Blue" };
	const int v = Picker_Variant();
	const rosterCharacter_t *c;
	const rosterVariant_t *rv;
	float y = rect->y + 4;
	int t;

	uiInfo.uiDC.fillRect( rect->x, rect->y, rect->w, rect->h, pickerPanel );
	if ( v < 0 ) {
		return;
	}
	c = &uiRoster.characters[picker.character];
	rv = &uiRoster.variants[v];
	Text_Paint( rect->x + 6, y, scale * 1.4f, color, c->name, 0, 0, textStyle, iMenuFont );
	y += Text_Height( c->name, scale * 1.4f, iMenuFont ) + 6;
	Text_Paint( rect->x + 6, y, scale * 0.75f, pickerDim, Picker_SourceName( c ), 0, 0, textStyle, iMenuFont );
	y += 24;
	Text_Paint( rect->x + 6, y, scale * 0.8f, pickerDim, "Outfit", 0, 0, textStyle, iMenuFont );
	y += 14;
	Text_Paint( rect->x + 6, y, scale, color, va( "%s  %d / %d", rv->name, picker.variant + 1, c->numVariants ), 0, 0, textStyle, iMenuFont );
	y += 30;
	Text_Paint( rect->x + 6, y, scale * 0.8f, pickerDim, "Team colour", 0, 0, textStyle, iMenuFont );
	y += 16;
	for ( t = 0; t < ROSTER_NUM_TEAMS; t++ ) {
		const qboolean have = (qboolean)( t == ROSTER_TEAM_DEFAULT || rv->skin[t][0] );
		rectDef_t *chip = &picker.chips[t];
		vec4_t col;
		chip->x = rect->x + 6 + t * ( ( rect->w - 12 ) / 3.0f );
		chip->y = y;
		chip->w = ( rect->w - 12 ) / 3.0f - 4;
		chip->h = 16;
		VectorCopy4( pickerChipColor[t], col );
		if ( !have ) col[3] = 0.25f;
		uiInfo.uiDC.fillRect( chip->x, chip->y, chip->w, chip->h, col );
		if ( t == (int)picker.team ) {
			uiInfo.uiDC.drawRect( chip->x - 1, chip->y - 1, chip->w + 2, chip->h + 2, 1, color );
		}
		Text_Paint( chip->x + 3, chip->y + 1, scale * 0.6f, have ? colorBlack : pickerDim, teamNames[t], 0, 0, 0, iMenuFont );
	}
}

qboolean UI_PickerOwnerDraw( const rectDef_t *rect, int ownerDraw, float scale, vec4_t color, int textStyle, int iMenuFont ) {
	switch ( ownerDraw ) {
	case UI_PICKER_ROSTER: Picker_PaintRoster( rect, scale, color, textStyle, iMenuFont ); return qtrue;
	case UI_PICKER_STAGE: Picker_PaintStage( rect, scale, color, textStyle, iMenuFont ); return qtrue;
	case UI_PICKER_VARIANTS: Picker_PaintVariants( rect, scale, color, textStyle, iMenuFont ); return qtrue;
	case UI_PICKER_DETAILS: Picker_PaintDetails( rect, scale, color, textStyle, iMenuFont ); return qtrue;
	default: return qfalse;
	}
}

/* ------------------------------------------------------------------ actions */

static void Picker_Team( int t ) {
	const int v = Picker_Variant();
	if ( v < 0 || t < 0 || t >= ROSTER_NUM_TEAMS || ( t != ROSTER_TEAM_DEFAULT && !uiRoster.variants[v].skin[t][0] ) ) {
		return;
	}
	if ( t != (int)picker.team ) {
		picker.team = (rosterTeam_t)t;
		picker.changed = uiInfo.uiDC.realTime;
	}
}

static void Picker_Leave( void ) {
	uiClientState_t cs;

	trap->GetClientState( &cs );
	if ( cs.connState >= CA_CONNECTED ) {	// in game: back to the game, as closeingame
		trap->Key_SetCatcher( trap->Key_GetCatcher() & ~KEYCATCH_UI );
		trap->Key_ClearStates();
		trap->Cvar_Set( "cl_paused", "0" );
		Menus_CloseAll();
	} else {
		Menus_CloseByName( PICKER_MENU );
		Menus_ActivateByName( "main" );
	}
}

static void Picker_Apply( void ) {
	char value[MAX_QPATH];
	const int v = Picker_Variant();

	if ( v < 0 ) {
		return;
	}
	UI_RosterModelValue( &uiRoster, v, picker.team, value, sizeof( value ) );
	trap->Cvar_Set( "model", value );		// as the grid (FEEDER_Q3HEADS) sets it
	trap->Cvar_Set( "char_color_red", "255" );
	trap->Cvar_Set( "char_color_green", "255" );
	trap->Cvar_Set( "char_color_blue", "255" );
	Picker_Leave();
}

static void Picker_Open( void ) {
	Picker_FromCvar();
	picker.changed = uiInfo.uiDC.realTime - PICKER_DWELL_MS;	// the current model loads at once
	picker.face = qfalse;
	picker.state = PICKER_RESTING;
	// the stage as the picker's own menu has it: idle, no sabers, the draft's skin (the player screen sets its own)
	picker.baseAnim = picker.gesture = -1;
	picker.preview = ROSTER_NUM_TEAMS;
	picker.sabers[0][0] = picker.sabers[1][0] = 0;
	picker.lit = qfalse;
	pickerFrom.zoom = pickerTo.zoom = 1.0f;
	pickerFrom.center = pickerTo.center = 0.5f;
	pickerFrom.yaw = pickerTo.yaw = PICKER_YAW;
	pickerPanelOn = qtrue;
	pickerFramed = qfalse;
	pickerFit.on = qfalse;
	pickerFit.shift = 0;
	pickerFit.zoom = 1.0f;
}

static void Picker_Close( void ) {
	Picker_FreeModel();
	picker.warmHandle = 0;
	UI_PickerStageCovers( qfalse );
}

qboolean UI_PickerScript( const char *name, char **args ) {
	int n;

	if ( Q_stricmpn( name, "picker", 6 ) ) {
		return qfalse;
	}
	if ( !Q_stricmp( name, "pickerOpen" ) ) {
		Picker_Open();
	} else if ( !Q_stricmp( name, "pickerClose" ) ) {
		Picker_Close();
	} else if ( !Q_stricmp( name, "pickerApply" ) ) {
		Picker_Apply();
	} else if ( !Q_stricmp( name, "pickerBack" ) ) {
		Picker_Leave();
	} else if ( !Q_stricmp( name, "pickerCharacter" ) ) {
		if ( Int_Parse( args, &n ) ) Picker_Select( picker.character + n, 0 );
	} else if ( !Q_stricmp( name, "pickerVariant" ) ) {
		if ( Int_Parse( args, &n ) ) Picker_Select( picker.character, picker.variant + n );
	} else if ( !Q_stricmp( name, "pickerTeam" ) ) {
		if ( Int_Parse( args, &n ) ) Picker_Team( n );
	} else if ( !Q_stricmp( name, "pickerFace" ) ) {
		picker.face = (qboolean)!picker.face;
	} else {
		return qfalse;
	}
	return qtrue;
}

static qboolean Picker_Inside( const rectDef_t *r ) {
	return (qboolean)( r->w > 0 && uiInfo.uiDC.cursorx >= r->x && uiInfo.uiDC.cursorx < r->x + r->w
		&& uiInfo.uiDC.cursory >= r->y && uiInfo.uiDC.cursory < r->y + r->h );
}

static qboolean Picker_Click( void ) {
	int t;

	if ( Picker_Inside( &picker.roster ) ) {
		const int c = picker.top + (int)( ( uiInfo.uiDC.cursory - picker.roster.y ) / PICKER_ROW_H );
		if ( c >= 0 && c < uiRoster.numCharacters ) {
			Picker_Select( c, c == picker.character ? picker.variant : 0 );
		}
		return qtrue;
	}
	if ( Picker_Inside( &picker.strip ) ) {
		Picker_Select( picker.character, picker.variant + ( uiInfo.uiDC.cursorx < picker.strip.x + picker.strip.w * 0.5f ? -1 : 1 ) );
		return qtrue;
	}
	for ( t = 0; t < ROSTER_NUM_TEAMS; t++ ) {
		if ( Picker_Inside( &picker.chips[t] ) ) {
			Picker_Team( t );
			return qtrue;
		}
	}
	return qfalse;	// a button, or nothing
}

qboolean UI_PickerMenuKey( menuDef_t *menu, int key, qboolean down ) {
	// the picker's own screen (the player screen drives the picker itself: ui_player_look.c)
	if ( !menu || !menu->window.name || Q_stricmp( menu->window.name, PICKER_MENU ) || !down || !Picker_Valid() ) {
		return qfalse;
	}
	switch ( key ) {
	case A_CURSOR_UP: case A_KP_8: case A_LOW_W: case A_CAP_W: case A_MWHEELUP:
		Picker_Select( picker.character - 1, 0 );
		return qtrue;
	case A_CURSOR_DOWN: case A_KP_2: case A_LOW_S: case A_CAP_S: case A_MWHEELDOWN:
		Picker_Select( picker.character + 1, 0 );
		return qtrue;
	case A_CURSOR_LEFT: case A_KP_4: case A_LOW_A: case A_CAP_A:
		Picker_Select( picker.character, picker.variant - 1 );
		return qtrue;
	case A_CURSOR_RIGHT: case A_KP_6: case A_LOW_D: case A_CAP_D:
		Picker_Select( picker.character, picker.variant + 1 );
		return qtrue;
	case A_1: Picker_Team( ROSTER_TEAM_DEFAULT ); return qtrue;
	case A_2: Picker_Team( ROSTER_TEAM_RED ); return qtrue;
	case A_3: Picker_Team( ROSTER_TEAM_BLUE ); return qtrue;
	case A_LOW_F: case A_CAP_F:
		picker.face = (qboolean)!picker.face;
		return qtrue;
	case A_ENTER: case A_KP_ENTER: {
		int i;
		for ( i = 0; i < menu->itemCount; i++ ) {
			if ( ( menu->items[i]->window.flags & WINDOW_HASFOCUS ) && menu->items[i]->type == ITEM_TYPE_BUTTON ) {
				return qfalse;	// the focused button's
			}
		}
		Picker_Apply();
		return qtrue;
	}
	case A_ESCAPE:
		trap->S_StartLocalSound( trap->S_RegisterSound( "sound/interface/esc.wav" ), CHAN_LOCAL_SOUND );
		Picker_Leave();
		return qtrue;
	case A_MOUSE1:
		return Picker_Click();
	default:
		return qfalse;
	}
}

/* ------------------------------------------------------------------ the player screen's draft */

// the selected outfit's model cvar value (as Apply would set it); qfalse when nothing is selected
qboolean UI_PickerDraftModel( char *buf, int size ) {
	const int v = Picker_Variant();

	buf[0] = 0;
	if ( v < 0 ) {
		return qfalse;
	}
	UI_RosterModelValue( &uiRoster, v, picker.team, buf, size );
	return qtrue;
}

// "Character, outfit" for the player screen's tab
void UI_PickerDraftName( char *buf, int size ) {
	const int v = Picker_Variant();

	buf[0] = 0;
	if ( v >= 0 ) {
		Com_sprintf( buf, size, "%s, %s", uiRoster.characters[picker.character].name, uiRoster.variants[v].name );
	}
}

// the player screen's handle on the picker (ui_player_look.c): the selection, the stage's state and framing

void UI_PickerGet( int *character, int *variant, int *team ) {
	*character = Picker_Valid() ? picker.character : -1;
	*variant = picker.variant;
	*team = (int)picker.team;
}

void UI_PickerSet( int character, int variant ) {
	Picker_Select( character, variant );
}

void UI_PickerSetTeam( int team ) {
	Picker_Team( team );
}

qboolean UI_PickerTeamAvailable( int team ) {
	const int v = Picker_Variant();
	return (qboolean)( v >= 0 && team >= 0 && team < ROSTER_NUM_TEAMS && ( team == ROSTER_TEAM_DEFAULT || uiRoster.variants[v].skin[team][0] ) );
}

void UI_PickerToggleFace( void ) {
	picker.face = (qboolean)!picker.face;
}

qboolean UI_PickerFace( void ) {
	return picker.face;
}

// 0 the model is on the stage, 1 waiting, 2 loading, 3 over the preview cap, 4 failed
int UI_PickerStageState( void ) {
	const int v = Picker_Variant();
	if ( Picker_Live( v ) ) {
		return 0;
	}
	switch ( picker.state ) {
	case PICKER_LOADING: return 2;
	case PICKER_CAPPED: return 3;
	case PICKER_FAILED: return 4;
	default: return 1;
	}
}

void UI_PickerStageFrame( float zoom, float center, float yaw, qboolean panel ) {
	pickerFrame_t to;

	to.zoom = zoom > 0.1f ? zoom : 1.0f;
	to.center = center;
	to.yaw = yaw;
	pickerPanelOn = panel;
	if ( !pickerFramed ) {	// the screen opens on its framing, no turn from the picker's default
		pickerFramed = qtrue;
		pickerFrom = pickerTo = to;
		pickerFrameStart = -PICKER_TWEEN_MS;
		pickerFit.snapUntil = uiInfo.uiDC.realTime + 250;
		return;
	}
	if ( to.zoom == pickerTo.zoom && to.center == pickerTo.center && to.yaw == pickerTo.yaw ) {
		return;
	}
	Picker_Frame( &pickerFrom );	// from where it is, mid-tween too
	pickerTo = to;
	pickerFrameStart = uiInfo.uiDC.realTime;
}

// whether the menu around the stage covers the whole screen (the player screen's backdrop): then the renderer leaves out
// the world under it (r_stageCovers), and the stage, the frame's first view, is drawn multisampled as in the main menu
void UI_PickerStageCovers( qboolean covers ) {
	if ( covers != pickerCovers ) {
		pickerCovers = covers;
		trap->Cvar_Set( "r_stageCovers", covers ? "1" : "0" );
	}
}

// from UI_Init and UI_Shutdown: a UI restart with the player screen open (vid_restart) runs no onClose, and the new
// module's pickerCovers starts false, so a "1" left in the cvar would stay
void UI_PickerStageCoversReset( void ) {
	pickerCovers = qfalse;
	if ( trap->Cvar_VariableValue( "r_stageCovers" ) != 0.0f ) {
		trap->Cvar_Set( "r_stageCovers", "0" );
	}
}

// the line the stage keeps the figure left of, as x at the top and the bottom of the 640x480 screen; the player
// screen sets it every frame (the picker's own menu has none)
void UI_PickerStageClear( float xTop, float xBottom ) {
	pickerFit.on = qtrue;
	pickerFit.xTop = xTop;
	pickerFit.xBottom = xBottom;
}

// the looped animation (-1: the outfit's idle)
void UI_PickerStagePose( int anim ) {
	picker.baseAnim = anim;
}

// an animation played once over the pose (-1: stop it)
void UI_PickerStageGesture( int anim ) {
	const animation_t *a = Picker_Anim( anim );

	picker.gesture = a ? anim : -1;
	picker.gestureEnds = a ? uiInfo.uiDC.realTime + a->numFrames * a->frameLerp : 0;
	picker.liveAnim = -1;	// restart it, even when it is the one shown
}

// the hilts in the hands (saber2 "" for one) and their blades (lit or not)
void UI_PickerStageSabers( const char *saber1, const char *saber2, int color1, int color2, qboolean lit ) {
	Q_strncpyz( picker.sabers[0], saber1 ? saber1 : "", sizeof( picker.sabers[0] ) );
	Q_strncpyz( picker.sabers[1], saber2 ? saber2 : "", sizeof( picker.sabers[1] ) );
	picker.colors[0] = (saber_colors_t)color1;
	picker.colors[1] = (saber_colors_t)color2;
	picker.lit = lit;
}

// a team skin shown instead of the draft's (-1: the draft's); an outfit without that skin keeps its own
void UI_PickerStagePreview( int team ) {
	picker.preview = team >= 0 && team < ROSTER_NUM_TEAMS ? (rosterTeam_t)team : ROSTER_NUM_TEAMS;
}

// a character's source tab label (character.cfg sourcelabel, else its tag in capitals; ui_roster.c)
const char *UI_PickerSourceTag( int character ) {
	const int s = character >= 0 && character < uiRoster.numCharacters ? uiRoster.characters[character].sourceIndex : -1;
	return s >= 0 && s < uiRoster.numSources ? uiRoster.sources[s].label : "OTHER";
}

/* ------------------------------------------------------------------ the menu */

void UI_PickerLoadMenu( void ) {
	fileHandle_t f;
	const int len = trap->FS_Open( PICKER_MENU_FILE, &f, FS_READ );

	if ( f ) {
		trap->FS_Close( f );
	}
	if ( len > 0 && !Menus_FindByName( PICKER_MENU ) ) {
		UI_ParseMenu( PICKER_MENU_FILE );
	}
}

void UI_Characters_f( void ) {
	if ( !Menus_FindByName( PICKER_MENU ) ) {
		Com_Printf( "ui_characters: no %s (the xenjk UI pack is not installed)\n", PICKER_MENU_FILE );
		return;
	}
	if ( !uiRoster.numCharacters ) {
		Com_Printf( "ui_characters: no characters listed\n" );
		return;
	}
	{
		menuDef_t *menu = Menus_FindByName( PICKER_MENU );
		uiClientState_t cs;
		trap->GetClientState( &cs );
		menu->fullScreen = (qboolean)( cs.connState < CA_CONNECTED );	// see the top of this file
		menu->window.backColor[3] = menu->fullScreen ? 1.0f : 0.6f;
	}
	Menus_CloseAll();
	if ( Menus_ActivateByName( PICKER_MENU ) ) {
		trap->Key_SetCatcher( KEYCATCH_UI );
	}
}
