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
The player screen's look: the redesign (docs/wishlist/player-screen.md 10.11; drawn from its HTML mockup, which is
not in the repository), drawn on the stock 2D path with no engine
change. ui_player.c keeps the sections, the draft and the commit; this file draws the screen and takes its input.

- Canvas. Everything is laid out on the mockup's 1920 x 1080 grid (the numbers below are the mockup's) and drawn as
  stretch pics mapped onto the UI's 640 x 480 (x / 3, y * 4 / 9), so the screen fills a 16:9 display as the mockup
  fills its frame.
- Type. The mockup's faces (Big Shoulders Display 900, Chakra Petch 700) as glyph atlases in fonts/xp_*.png, with
  the screen's one shear (-12 degrees) baked in (the upright twins dispu / tagu for list words and hints) and a
  blurred twin for the text shadow (tools/ui/bake_xen_player_art.py). This file draws them glyph by glyph, so text
  lands on fractional canvas positions and the selected list word can be cut (top half +6, bottom -4).
- Shapes. Sheared boxes are a middle fill and two diagonal ends (gfx/xp/cap); brushes, dots and the backdrop per
  section (gfx/xp/bg<n>, the mockup's painted world with its bloom) are pictures.
- Structure, as the mockup: every section is HEAD (the filter or constraint) > LIST (the decision) > DETAIL (what
  depends on the selection) > COMMIT, and Tab walks those zones. Up / Down move in a zone, Left / Right change the
  focused value, Enter presses, Q / E change section, F the face view, Esc goes back. The mouse focuses what it is
  over (not the list, where focus is the selection), clicks press, right click lowers a Force power, the wheel moves
  in the list.
- Controls. Nothing here writes a stock cvar or the Force state itself: the picker (ui_picker.c) keeps the character,
  and the Saber and Force controls press the screen's hidden stock items (XP_PressItem / XP_SelectRow / XP_ItemKey),
  so the rules and the cvars are the stock ones.
- Without the art (no fonts/xp_*.xfont) the screen still works: text falls back to the stock font.
*/

#include "ui_local.h"
#include "ui_force.h"
#include "ui_player.h"


#define LK_TAN12		0.21255656f		// the one shear
#define LK_SLOPE		( -LK_TAN12 )	// rows step down the shear line
#define LK_MAX_EMS		3
#define LK_MAX_HITS		96
// a list holds every row its source can have: the roster's characters (one filter can show them all) or the hilts
#define LK_MAX_ROWS		( ROSTER_MAX_CHARACTERS > MAX_SABER_HILTS ? ROSTER_MAX_CHARACTERS : MAX_SABER_HILTS )
#define LK_MAX_FIELDS	6
#define LK_MAX_HEAD		4
#define LK_MAX_BUTTONS	5

/* ------------------------------------------------------------------ colours */

static const vec4_t lkInk = { 0.043f, 0.047f, 0.063f, 1.0f };
static const vec4_t lkPaper = { 0.957f, 0.957f, 0.937f, 1.0f };
static const vec4_t lkAcc = { 0.212f, 0.871f, 0.933f, 1.0f };
static const vec4_t lkSec = { 0.506f, 0.659f, 1.0f, 1.0f };
static const vec4_t lkWarn = { 1.0f, 0.231f, 0.290f, 1.0f };
static const vec4_t lkRed = { 0.898f, 0.282f, 0.302f, 1.0f };
static const vec4_t lkBlue = { 0.243f, 0.545f, 1.0f, 1.0f };
static const vec4_t lkHint = { 0.702f, 0.718f, 0.776f, 1.0f };

// the stock blade colour names (ui_saber_color), in the mockup's order, and how they show
static const char *lkBladeNames[6] = { "red", "orange", "yellow", "green", "blue", "purple" };
static const vec4_t lkBlades[6] = {
	{ 1.0f, 0.231f, 0.231f, 1.0f }, { 1.0f, 0.541f, 0.165f, 1.0f }, { 1.0f, 0.824f, 0.227f, 1.0f },
	{ 0.294f, 0.890f, 0.416f, 1.0f }, { 0.247f, 0.663f, 1.0f, 1.0f }, { 0.659f, 0.420f, 1.0f, 1.0f }
};
static const vec4_t lkTeamChips[3] = { { 0.851f, 0.839f, 0.812f, 1.0f }, { 0.898f, 0.282f, 0.302f, 1.0f }, { 0.243f, 0.545f, 1.0f, 1.0f } };

static void LK_Col( vec4_t out, const float *c, float alpha ) {
	out[0] = c[0]; out[1] = c[1]; out[2] = c[2]; out[3] = c[3] * alpha;
}

/* ------------------------------------------------------------------ the art */

typedef struct lkGlyph_s {
	float	adv;			// pixels at the em
	float	x, y, w, h;		// the cell in the atlas
	float	ox, oy;			// the cell's top left from the pen on the baseline
} lkGlyph_t;

typedef struct lkAtlas_s {
	int			em;
	float		asc, desc, tw, th;
	qhandle_t	image, shadow;
	lkGlyph_t	g[256];
	byte		have[256];
} lkAtlas_t;

enum { LK_DISP, LK_DISPU, LK_TAG, LK_TAGU, LK_NUM_FACES };

typedef struct lkFace_s {
	const char	*name;
	int			ems[LK_MAX_EMS];
	int			num;
	lkAtlas_t	at[LK_MAX_EMS];
} lkFace_t;

static lkFace_t lkFaces[LK_NUM_FACES] = {
	{ "disp", { 32, 64, 128 } }, { "dispu", { 32, 64, 128 } }, { "tag", { 18, 36 } }, { "tagu", { 18, 36 } }
};

enum { LKH_TAB, LKH_HEAD, LKH_LIST, LKH_FIELD, LKH_ARROW, LKH_CHIP, LKH_COMMIT, LKH_NAME };

typedef struct lkHit_s {
	float	x, y, w, h;		// canvas units
	int		kind, idx, sub;
} lkHit_t;

enum { LKZ_HEAD, LKZ_LIST, LKZ_DETAIL, LKZ_COMMIT };
enum { LKE_NONE, LKE_NAME, LKE_SAVE };

static struct {
	qboolean	loaded, tried;
	qhandle_t	white, cap, dot, glow, grad, brushC, brushP, bg[XP_NUM_SECTIONS];

	int			zone;
	int			hf[XP_NUM_SECTIONS], df[XP_NUM_SECTIONS], focus[XP_NUM_SECTIONS], bf;
	int			filter;			// Character: the source filter (0 = all)
	int			hand;			// Saber, dual: the hand the list sets
	int			tpl;			// Force: the template the field shows
	int			edit;			// LKE_*: a text field takes the keys
	int			editStart;
	int			lastSection;	// the section the zones were set for
	int			stance;			// Saber, single: the stance the stage shows (0 Fast, 1 Medium, 2 Strong; -1 the default)
	int			gestureFor;		// Force: the power whose gesture the stage played (-1 none)
	int			stageSection;	// the section the stage was set for

	lkHit_t		hits[LK_MAX_HITS];
	int			numHits;
	float		mouseX, mouseY;	// last cursor (canvas)
	int			hover;			// hit under the cursor, or -1
} lk;

static int LK_ReadFile( const char *path, char *buf, int size ) {
	fileHandle_t f;
	int len = trap->FS_Open( path, &f, FS_READ );

	if ( !f ) {
		return 0;
	}
	if ( len <= 0 || len >= size ) {
		trap->FS_Close( f );
		return 0;
	}
	trap->FS_Read( buf, len, f );
	buf[len] = 0;
	trap->FS_Close( f );
	return len;
}

static qboolean LK_LoadAtlas( lkAtlas_t *a, const char *face, int em ) {
	static char buf[16384];
	const char *p = buf, *tok;
	char path[MAX_QPATH];

	Com_sprintf( path, sizeof( path ), "fonts/xp_%s_%d.xfont", face, em );
	if ( !LK_ReadFile( path, buf, sizeof( buf ) ) ) {
		return qfalse;
	}
	memset( a, 0, sizeof( *a ) );
	while ( ( tok = COM_ParseExt( &p, qtrue ) ) != NULL && tok[0] ) {
		if ( !Q_stricmp( tok, "em" ) ) {
			a->em = atoi( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "ascent" ) ) {
			a->asc = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "descent" ) ) {
			a->desc = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "size" ) ) {
			a->tw = atof( COM_ParseExt( &p, qfalse ) );
			a->th = atof( COM_ParseExt( &p, qfalse ) );
		} else if ( !Q_stricmp( tok, "g" ) ) {
			const int code = atoi( COM_ParseExt( &p, qfalse ) ) & 255;
			lkGlyph_t *g = &a->g[code];
			g->adv = atof( COM_ParseExt( &p, qfalse ) );
			g->x = atof( COM_ParseExt( &p, qfalse ) );
			g->y = atof( COM_ParseExt( &p, qfalse ) );
			g->w = atof( COM_ParseExt( &p, qfalse ) );
			g->h = atof( COM_ParseExt( &p, qfalse ) );
			g->ox = atof( COM_ParseExt( &p, qfalse ) );
			g->oy = atof( COM_ParseExt( &p, qfalse ) );
			a->have[code] = 1;
		}
	}
	if ( a->em <= 0 || a->tw <= 0 || a->th <= 0 ) {
		return qfalse;
	}
	a->image = trap->R_RegisterShaderNoMip( va( "fonts/xp_%s_%d", face, em ) );
	a->shadow = trap->R_RegisterShaderNoMip( va( "fonts/xp_%s_%d_s", face, em ) );
	return (qboolean)( a->image != 0 );
}

static void LK_Load( void ) {
	int f, i;

	if ( lk.tried ) {
		return;
	}
	lk.tried = qtrue;
	lk.loaded = qtrue;
	for ( f = 0; f < LK_NUM_FACES; f++ ) {
		lkFace_t *face = &lkFaces[f];
		face->num = 0;
		for ( i = 0; i < LK_MAX_EMS && face->ems[i]; i++ ) {
			if ( LK_LoadAtlas( &face->at[face->num], face->name, face->ems[i] ) ) {
				face->num++;
			}
		}
		if ( !face->num ) {
			lk.loaded = qfalse;
		}
	}
	if ( !lk.loaded ) {
		Com_Printf( S_COLOR_YELLOW "player screen: no fonts/xp_*.xfont (the look's art); using the stock font\n" );
	}
	lk.white = trap->R_RegisterShaderNoMip( "gfx/xp/white" );
	lk.cap = trap->R_RegisterShaderNoMip( "gfx/xp/cap" );
	lk.dot = trap->R_RegisterShaderNoMip( "gfx/xp/dot" );
	lk.glow = trap->R_RegisterShaderNoMip( "gfx/xp/glow" );
	lk.grad = trap->R_RegisterShaderNoMip( "gfx/xp/grad" );
	lk.brushC = trap->R_RegisterShaderNoMip( "gfx/xp/brush_c" );
	lk.brushP = trap->R_RegisterShaderNoMip( "gfx/xp/brush_p" );
	for ( i = 0; i < XP_NUM_SECTIONS; i++ ) {
		lk.bg[i] = trap->R_RegisterShaderNoMip( va( "gfx/xp/bg%d", i ) );
	}
	if ( !lk.white ) {
		lk.white = uiInfo.uiDC.whiteShader;
	}
}

/* ------------------------------------------------------------------ drawing on the canvas */

#define LK_X( x )	( ( x ) * ( 1.0f / 3.0f ) )
#define LK_Y( y )	( ( y ) * ( 4.0f / 9.0f ) )

static void LK_PicST( float x, float y, float w, float h, float s0, float t0, float s1, float t1, qhandle_t shader, const float *rgba ) {
	if ( !shader || w <= 0 || h <= 0 || ( rgba && rgba[3] <= 0.0f ) ) {
		return;
	}
	trap->R_SetColor( rgba );
	trap->R_DrawStretchPic( LK_X( x ), LK_Y( y ), LK_X( w ), LK_Y( h ), s0, t0, s1, t1, shader );
	trap->R_SetColor( NULL );
}

static void LK_Pic( float x, float y, float w, float h, qhandle_t shader, const float *rgba ) {
	LK_PicST( x, y, w, h, 0, 0, 1, 1, shader, rgba );
}

static void LK_Rect( float x, float y, float w, float h, const float *rgba ) {
	LK_Pic( x, y, w, h, lk.white, rgba );
}

// a box sheared by tan t about its middle (CSS skewX(-atan t)): the top moves right by t * h / 2, the bottom left
static void LK_Para( float x, float y, float w, float h, float t, const float *rgba ) {
	float s = t * h * 0.5f;

	if ( w <= 0 || h <= 0 ) {
		return;
	}
	if ( s * 2.0f > w ) {
		s = w * 0.5f;
	}
	if ( !lk.cap ) {
		LK_Rect( x, y, w, h, rgba );
		return;
	}
	LK_PicST( x - s, y, 2.0f * s, h, 0, 0, 1, 1, lk.cap, rgba );
	LK_Rect( x + s, y, w - 2.0f * s, h, rgba );
	LK_PicST( x + w - s, y, 2.0f * s, h, 1, 1, 0, 0, lk.cap, rgba );
}

// an outlined sheared box: the ring, then the inside
static void LK_ParaRing( float x, float y, float w, float h, float t, float ring, const float *ringRgba, const float *fill ) {
	LK_Para( x, y, w, h, t, ringRgba );
	LK_Para( x + ring, y + ring, w - 2 * ring, h - 2 * ring, t, fill );
}

static void LK_Brush( float x, float y, float w, float h, qboolean paper, float alpha ) {
	vec4_t c = { 1, 1, 1, 1 };
	c[3] = alpha;
	LK_Pic( x, y, w, h, paper ? lk.brushP : lk.brushC, c );
}

/* ------------------------------------------------------------------ type */

#define LKT_SHADOW		1
#define LKT_UPPER		2

static const lkAtlas_t *LK_Atlas( int face, float size ) {
	const lkFace_t *f = &lkFaces[face];
	const float px = size * uiInfo.uiDC.glconfig.vidHeight / 1080.0f;
	int i;

	if ( !f->num ) {
		return NULL;
	}
	for ( i = 0; i < f->num; i++ ) {
		if ( f->at[i].em >= px * 0.85f ) {
			return &f->at[i];
		}
	}
	return &f->at[f->num - 1];
}

static const char *LK_Upper( const char *s ) {
	static char buf[256];
	int i;

	for ( i = 0; s[i] && i < (int)sizeof( buf ) - 1; i++ ) {
		buf[i] = ( s[i] >= 'a' && s[i] <= 'z' ) ? s[i] - 32 : s[i];
	}
	buf[i] = 0;
	return buf;
}

// the next character to draw (colour codes skipped)
static int LK_Next( const char **p ) {
	const unsigned char *s = (const unsigned char *)*p;

	while ( s[0] == '^' && s[1] >= '0' && s[1] <= '9' ) {
		s += 2;
	}
	*p = (const char *)( s[0] ? s + 1 : s );
	return s[0];
}

static float LK_Width( int face, float size, float spacing, const char *s, int flags ) {
	const lkAtlas_t *a = LK_Atlas( face, size );
	float w = 0, k;
	int c;

	if ( !a ) {
		return Text_Width( s, size / 40.0f, FONT_MEDIUM ) * 3.0f;
	}
	if ( flags & LKT_UPPER ) {
		s = LK_Upper( s );
	}
	k = size / a->em;
	while ( ( c = LK_Next( &s ) ) != 0 ) {
		w += ( ( a->have[c] ? a->g[c].adv : a->g['?'].adv ) + spacing * a->em ) * k;
	}
	return w;
}

// the baseline of a CSS line box: top, font size, line-height (a factor; 0 = the font's normal)
static float LK_Base( int face, float top, float size, float lh ) {
	const lkFace_t *f = &lkFaces[face];
	float asc = 0.8f, desc = 0.25f, line;

	if ( f->num ) {
		asc = f->at[0].asc / f->at[0].em;
		desc = f->at[0].desc / f->at[0].em;
	}
	line = lh > 0 ? lh * size : ( asc + desc ) * size;
	return top + ( line - ( asc + desc ) * size ) * 0.5f + asc * size;
}

static float LK_Line( int face, float size, float lh ) {
	const lkFace_t *f = &lkFaces[face];
	if ( lh > 0 ) {
		return lh * size;
	}
	return f->num ? ( f->at[0].asc + f->at[0].desc ) / f->at[0].em * size : 1.2f * size;
}

typedef struct lkCut_s {
	float	y0, y1;		// draw only this band (canvas y)
	float	dx;
} lkCut_t;

static void LK_Run( const lkAtlas_t *a, qhandle_t shader, float x, float base, float size, float spacing, const float *rgba,
	const char *s, const lkCut_t *cut ) {
	const float k = size / a->em;
	int c;

	trap->R_SetColor( rgba );
	while ( ( c = LK_Next( &s ) ) != 0 ) {
		const lkGlyph_t *g = &a->g[a->have[c] ? c : '?'];
		if ( c != ' ' && g->w > 0 ) {
			float gx = x + g->ox * k, gy = base + g->oy * k, gw = g->w * k, gh = g->h * k;
			float t0 = g->y / a->th, t1 = ( g->y + g->h ) / a->th;
			qboolean draw = qtrue;
			if ( cut ) {
				if ( cut->y0 > gy ) {
					const float f = ( cut->y0 - gy ) / gh;
					if ( f >= 1.0f ) draw = qfalse;
					else { t0 += ( t1 - t0 ) * f; gh -= cut->y0 - gy; gy = cut->y0; }
				}
				if ( draw && cut->y1 < gy + gh ) {
					const float f = ( gy + gh - cut->y1 ) / gh;
					if ( f >= 1.0f ) draw = qfalse;
					else { t1 -= ( t1 - t0 ) * f; gh = cut->y1 - gy; }
				}
				gx += cut->dx;
			}
			if ( draw ) {
				trap->R_DrawStretchPic( LK_X( gx ), LK_Y( gy ), LK_X( gw ), LK_Y( gh ), g->x / a->tw, t0, ( g->x + g->w ) / a->tw, t1, shader );
			}
		}
		x += ( g->adv + spacing * a->em ) * k;
	}
	trap->R_SetColor( NULL );
}

/*
Text in a CSS line box: x is the box's left, top its top, lh its line-height factor (0 = normal); spacing is the
letter spacing in em. A sheared face shears about the baseline; the mockup shears each element about its middle, so
the pen moves by tan 12 * (pivot - baseline): pivot is the sheared element's middle (< 0: the line box's). Returns the
width.
*/
static float LK_Text( int face, float x, float top, float size, float lh, float spacing, float pivot, const float *rgba,
	const char *s, int flags ) {
	const lkAtlas_t *a = LK_Atlas( face, size );
	const float base = LK_Base( face, top, size, lh );
	float pen = x;

	if ( flags & LKT_UPPER ) {
		s = LK_Upper( s );
	}
	if ( !a ) {
		vec4_t c;
		LK_Col( c, rgba, 1 );
		Text_Paint( LK_X( x ), LK_Y( top ), size / 48.0f, c, s, 0, 0, 0, FONT_MEDIUM );
		return LK_Width( face, size, spacing, s, 0 );
	}
	if ( face == LK_DISP || face == LK_TAG ) {
		if ( pivot < 0 ) {
			pivot = top + LK_Line( face, size, lh ) * 0.5f;
		}
		pen += LK_TAN12 * ( pivot - base );
	}
	if ( flags & LKT_SHADOW ) {
		vec4_t sh;
		LK_Col( sh, lkInk, 0.85f * rgba[3] );
		LK_Run( a, a->shadow, pen, base + 3.0f, size, spacing, sh, s, NULL );
	}
	LK_Run( a, a->image, pen, base, size, spacing, rgba, s, NULL );
	return LK_Width( face, size, spacing, s, 0 );
}

/* ------------------------------------------------------------------ hits */

static void LK_Hit( float x, float y, float w, float h, int kind, int idx, int sub ) {
	lkHit_t *t;

	if ( lk.numHits >= LK_MAX_HITS ) {
		return;
	}
	t = &lk.hits[lk.numHits++];
	t->x = x; t->y = y; t->w = w; t->h = h;
	t->kind = kind; t->idx = idx; t->sub = sub;
}

// the last hit added under the canvas point (later ones are on top), or -1
static int LK_HitAt( float x, float y ) {
	int i;

	for ( i = lk.numHits - 1; i >= 0; i-- ) {
		const lkHit_t *t = &lk.hits[i];
		if ( x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h ) {
			return i;
		}
	}
	return -1;
}

static qboolean LK_Hovered( int kind, int idx, int sub ) {
	const lkHit_t *t;

	if ( lk.hover < 0 || lk.hover >= lk.numHits ) {
		return qfalse;
	}
	t = &lk.hits[lk.hover];
	return (qboolean)( t->kind == kind && t->idx == idx && ( sub < 0 || t->sub == sub ) );
}

/* ------------------------------------------------------------------ components */

enum { LKS_PAPER, LKS_V, LKS_N, LKS_LT, LKS_DK, LKS_RED, LKS_BLUE, LKS_MUTE, LKS_COSTNO, LKS_MAX };

typedef struct lkTag_s {
	char	t[40];
	int		style;
} lkTag_t;

// a tag (.tg: 700 15px, letter spacing .04em, padding 5 9 4, sheared): x, top; returns the width
static float LK_TagDraw( float x, float top, const char *t, int style, float alpha ) {
	const float tw = LK_Width( LK_TAG, 15, 0.04f, t, LKT_UPPER ), w = tw + 18, h = 24;
	vec4_t bg, fg;

	switch ( style ) {
	case LKS_V: case LKS_MAX: LK_Col( bg, lkAcc, alpha ); LK_Col( fg, lkInk, alpha ); break;
	case LKS_N: LK_Col( bg, lkSec, 0.7f * alpha ); LK_Col( fg, lkSec, alpha ); break;
	case LKS_LT: LK_Col( bg, lkSec, alpha ); LK_Col( fg, lkInk, alpha ); break;
	case LKS_DK: case LKS_COSTNO: LK_Col( bg, lkWarn, alpha ); LK_Col( fg, lkInk, alpha ); break;
	case LKS_RED: LK_Col( bg, lkRed, alpha ); LK_Col( fg, lkPaper, alpha ); break;
	case LKS_BLUE: LK_Col( bg, lkBlue, alpha ); LK_Col( fg, lkPaper, alpha ); break;
	case LKS_MUTE: LK_Col( bg, lkPaper, 0.14f * alpha ); LK_Col( fg, lkPaper, 0.7f * alpha ); break;
	default: LK_Col( bg, lkPaper, 0.9f * alpha ); LK_Col( fg, lkInk, alpha ); break;
	}
	if ( style == LKS_N ) {
		vec4_t in;
		LK_Col( in, lkInk, 0.75f * alpha );
		LK_ParaRing( x, top, w, h, LK_TAN12, 1.5f, bg, in );
	} else {
		LK_Para( x, top, w, h, LK_TAN12, bg );
	}
	LK_Text( LK_TAG, x + 9, top + 5, 15, 1.0f, 0.04f, top + h * 0.5f, fg, t, LKT_UPPER );
	return w;
}

// pips (three sheared boxes, 22 x 13, skew 24 degrees): x, middle; returns the width
static float LK_Pips( float x, float mid, int level, float alpha ) {
	int k;
	vec4_t on, ring, in;

	LK_Col( on, lkAcc, alpha );
	LK_Col( ring, lkPaper, 0.8f * alpha );
	LK_Col( in, lkInk, 0.7f * alpha );
	for ( k = 0; k < 3; k++ ) {
		const float px = x + k * 27.0f;
		if ( k < level ) {
			LK_Para( px, mid - 6.5f, 22, 13, 0.4452f, on );
		} else {
			LK_ParaRing( px, mid - 6.5f, 22, 13, 0.4452f, 2.0f, ring, in );
		}
	}
	return 3 * 22 + 2 * 5;
}

// the selected word's brush (.slash), behind a word box
static void LK_Slash( float x, float y, float w, float h, float s, float alpha ) {
	LK_Brush( x - 12 * s, y - 12 * s, w + 28 * s, h + 20 * s, qfalse, alpha );
}

/*
A list word (.word: upright 900 64px / .9, padding 4 16 2 10), scaled by s from its box's bottom left at (x, bottom).
cur: the selected row (the brush, ink type, the cut); active: the list is the focused zone. Returns the box width.
*/
static float LK_Word( float x, float bottom, float s, const char *word, qboolean cur, qboolean active, float alpha, qboolean hover ) {
	const float size = 64 * s, line = 57.6f * s, boxH = 63.6f * s, top = bottom - boxH, textTop = top + 4 * s;
	const float tw = LK_Width( LK_DISPU, size, 0, word, LKT_UPPER ), boxW = tw + 26 * s;
	vec4_t c;

	if ( cur ) {
		LK_Slash( x, top, boxW, boxH, s, active ? alpha : 0.35f * alpha );
	}
	if ( cur && active ) {
		const lkAtlas_t *a = LK_Atlas( LK_DISPU, size );
		const float base = LK_Base( LK_DISPU, textTop, size, 0.9f ), split = textTop + line * 0.54f;
		lkCut_t up, low;
		LK_Col( c, lkInk, alpha );
		up.y0 = textTop - line; up.y1 = split; up.dx = 6 * s;
		low.y0 = split; low.y1 = textTop + line * 2; low.dx = -4 * s;
		if ( a ) {
			const char *u = LK_Upper( word );
			LK_Run( a, a->image, x + 10 * s, base, size, 0, c, u, &up );
			LK_Run( a, a->image, x + 10 * s, base, size, 0, c, u, &low );
		} else {
			LK_Text( LK_DISPU, x + 10 * s, textTop, size, 0.9f, 0, -1, c, word, LKT_UPPER );
		}
	} else {
		LK_Col( c, hover ? lkAcc : lkPaper, alpha );
		LK_Text( LK_DISPU, x + 10 * s, textTop, size, 0.9f, 0, -1, c, word, LKT_UPPER | ( cur && active ? 0 : LKT_SHADOW ) );
	}
	return boxW;
}

/* ------------------------------------------------------------------ the section models */

typedef struct lkRow_s {
	char		word[48];
	int			id;			// Character: roster index; Saber: hilt row; Force: power; Team: card
	int			group;		// Force: 0 neutral, 1 light, 2 dark, 3 saber
	char		why[96];	// a reason it is refused ("" when it is not)
	int			pips;		// Force: the rank (-1: none)
	lkTag_t		tags[4];
	int			numTags;
	char		pl[96];		// Team: who is on it
	char		what[12];	// Team: the commit
} lkRow_t;

enum { LKF_VALUE, LKF_STATIC, LKF_BUTTON, LKF_TEAMCHIPS, LKF_BLADE1, LKF_BLADE2 };
enum { LKA_NONE, LKA_OUTFIT, LKA_TEAMCOL, LKA_CLASSIC, LKA_FACE, LKA_BLADE1, LKA_BLADE2, LKA_TEMPLATE, LKA_SAVE };

typedef struct lkField_s {
	char		k[32], v[64], n[48], off[96];
	int			kind, act;
	qboolean	arrows;
} lkField_t;

enum { LKHD_SEG, LKHD_TEXT, LKHD_METER };
enum { LKHA_SOURCE, LKHA_TYPE, LKHA_HAND, LKHA_SIDE, LKHA_CLASS };

typedef struct lkHead_s {
	int			kind, act;
	char		k[24], txt[96], off[96];
	char		opts[8][16];
	int			numOpts, cur;
} lkHead_t;

typedef struct lkButton_s {
	char		t[24], what[12];
	qboolean	primary;
	const float	*swatch;
} lkButton_t;

typedef struct lkModel_s {
	xpContext_t	c;
	int			sec;
	lkRow_t		rows[LK_MAX_ROWS];
	int			numRows, cur;			// the row with the selection (or the focus)
	lkField_t	fields[LK_MAX_FIELDS];
	int			numFields;
	lkHead_t	head[LK_MAX_HEAD];
	int			numHead;
	lkButton_t	buttons[LK_MAX_BUTTONS];
	int			numButtons;
} lkModel_t;

static lkModel_t lkm;

static void LK_AddTag( lkRow_t *r, const char *t, int style ) {
	if ( r->numTags < 4 ) {
		Q_strncpyz( r->tags[r->numTags].t, t, sizeof( r->tags[0].t ) );
		r->tags[r->numTags++].style = style;
	}
}

static lkRow_t *LK_AddRow( const char *word, int id ) {
	lkRow_t *r;

	if ( lkm.numRows >= LK_MAX_ROWS ) {
		return NULL;
	}
	r = &lkm.rows[lkm.numRows++];
	memset( r, 0, sizeof( *r ) );
	Q_strncpyz( r->word, word, sizeof( r->word ) );
	r->id = id;
	r->pips = -1;
	return r;
}

static lkField_t *LK_AddField( const char *k, int kind, int act ) {
	lkField_t *f;

	if ( lkm.numFields >= LK_MAX_FIELDS ) {
		return NULL;
	}
	f = &lkm.fields[lkm.numFields++];
	memset( f, 0, sizeof( *f ) );
	Q_strncpyz( f->k, k, sizeof( f->k ) );
	f->kind = kind;
	f->act = act;
	return f;
}

static lkHead_t *LK_AddHead( int kind, int act, const char *k ) {
	lkHead_t *h;

	if ( lkm.numHead >= LK_MAX_HEAD ) {
		return NULL;
	}
	h = &lkm.head[lkm.numHead++];
	memset( h, 0, sizeof( *h ) );
	h->kind = kind;
	h->act = act;
	Q_strncpyz( h->k, k, sizeof( h->k ) );
	return h;
}

static void LK_AddOpt( lkHead_t *h, const char *o ) {
	if ( h->numOpts < 8 ) {
		Q_strncpyz( h->opts[h->numOpts++], o, sizeof( h->opts[0] ) );
	}
}

static void LK_AddButton( const char *t, const char *what, qboolean primary, const float *swatch ) {
	lkButton_t *b;

	if ( lkm.numButtons >= LK_MAX_BUTTONS ) {
		return;
	}
	b = &lkm.buttons[lkm.numButtons++];
	Q_strncpyz( b->t, t, sizeof( b->t ) );
	Q_strncpyz( b->what, what, sizeof( b->what ) );
	b->primary = primary;
	b->swatch = swatch;
}

static qboolean LK_TeamMode( int gt ) {
	return (qboolean)( gt >= GT_TEAM && gt != GT_SIEGE );
}

/* Character */

// the source filters: All, then the roster's source tabs in its order (the tags the installed character.cfg files
// declare, then the stock models and other: ui_roster.c). Past the segment's room, the last tab is OTHER and holds
// the characters of the tabs that did not fit.
#define LK_MAX_SOURCES	8	// lkHead_t opts

static int LK_Sources( const char **out, int *tabOfSource ) {
	const int room = LK_MAX_SOURCES - 1;
	int n = 0, s, stock = -1, other = -1, dataRoom, kept = 0;

	out[n++] = "ALL";
	for ( s = 0; s < uiRoster.numSources; s++ ) {
		tabOfSource[s] = -1;
		if ( !Q_stricmp( uiRoster.sources[s].tag, ROSTER_SOURCE_STOCK ) ) stock = s;
		if ( !Q_stricmp( uiRoster.sources[s].tag, ROSTER_SOURCE_OTHER ) ) other = s;
	}
	// the stock models and OTHER always keep their tabs when the rest do not fit
	dataRoom = uiRoster.numSources > room ? room - ( stock >= 0 ) - 1 : room;
	for ( s = 0; s < uiRoster.numSources; s++ ) {	// sorted: the built-in tabs last
		if ( s != stock && s != other && kept++ >= dataRoom ) {
			continue;	// no room: its characters are under OTHER
		}
		tabOfSource[s] = n;
		out[n++] = uiRoster.sources[s].label;
	}
	if ( uiRoster.numSources > room && other < 0 ) {
		out[n++] = "OTHER";
	}
	for ( s = 0; s < uiRoster.numSources; s++ ) {
		if ( tabOfSource[s] < 0 ) tabOfSource[s] = n - 1;
	}
	return n;
}

static qboolean LK_InFilter( int c, int filter, const int *tabOfSource ) {
	const int s = uiRoster.characters[c].sourceIndex;
	return (qboolean)( filter == 0 || ( s >= 0 && s < uiRoster.numSources && tabOfSource[s] == filter ) );
}

static void LK_ModelCharacter( void ) {
	const char *sources[LK_MAX_SOURCES];
	int tabOfSource[ROSTER_MAX_SOURCES];
	const int numSources = LK_Sources( sources, tabOfSource );
	int character, variant, team, c, shown = 0;
	lkHead_t *h;
	lkField_t *f;

	UI_PickerGet( &character, &variant, &team );
	if ( lk.filter >= numSources ) {
		lk.filter = 0;
	}
	h = LK_AddHead( LKHD_SEG, LKHA_SOURCE, "SOURCE" );
	for ( c = 0; c < numSources; c++ ) {
		LK_AddOpt( h, sources[c] );
	}
	h->cur = lk.filter;
	lkm.cur = -1;
	for ( c = 0; c < uiRoster.numCharacters; c++ ) {
		const rosterCharacter_t *ch = &uiRoster.characters[c];
		lkRow_t *r;
		if ( !LK_InFilter( c, lk.filter, tabOfSource ) ) continue;
		shown++;
		if ( ( r = LK_AddRow( ch->name, c ) ) == NULL ) continue;
		LK_AddTag( r, UI_PickerSourceTag( c ), LKS_MUTE );
		LK_AddTag( r, va( "%d outfit%s", ch->numVariants, ch->numVariants == 1 ? "" : "s" ), LKS_N );
		if ( c == character ) lkm.cur = lkm.numRows - 1;
	}
	h = LK_AddHead( LKHD_TEXT, 0, "" );
	Com_sprintf( h->txt, sizeof( h->txt ), "%d characters", shown );

	if ( character >= 0 ) {
		const rosterCharacter_t *ch = &uiRoster.characters[character];
		const rosterVariant_t *rv = &uiRoster.variants[ch->firstVariant + variant];
		f = LK_AddField( "OUTFIT", LKF_VALUE, LKA_OUTFIT );
		Q_strncpyz( f->v, rv->name, sizeof( f->v ) );
		Com_sprintf( f->n, sizeof( f->n ), "%d of %d", variant + 1, ch->numVariants );
		f->arrows = (qboolean)( ch->numVariants > 1 );
		f = LK_AddField( "TEAM COLOUR", LKF_TEAMCHIPS, LKA_TEAMCOL );
		if ( lkm.c.connected && LK_TeamMode( lkm.c.gametype ) ) {
			Q_strncpyz( f->off, "Your team sets this colour", sizeof( f->off ) );
		}
		f = LK_AddField( "VIEW", LKF_VALUE, LKA_FACE );
		Q_strncpyz( f->v, UI_PickerFace() ? "Face" : "Full body", sizeof( f->v ) );
		Q_strncpyz( f->n, "F", sizeof( f->n ) );
		f->arrows = qtrue;
	}
	f = LK_AddField( "STOCK GRID", LKF_BUTTON, LKA_CLASSIC );
	Q_strncpyz( f->v, "Classic picker", sizeof( f->v ) );
}

/* Saber */

static int LK_SaberType( void ) {
	char t[16];

	trap->Cvar_VariableStringBuffer( "ui_saber_type", t, sizeof( t ) );
	return !Q_stricmp( t, "dual" ) ? 1 : !Q_stricmp( t, "staff" ) ? 2 : 0;
}

// the stock list that sets the hilt for the type and hand
static const char *LK_HiltList( int type, int hand ) {
	return type == 2 ? "hiltbut_staves" : type == 1 ? ( hand ? "hiltbut2" : "hiltbut1" ) : "hiltbut";
}

static int LK_HiltRow( int type, const char *cvar ) {
	char cur[MAX_QPATH];
	const char **info = type == 2 ? saberStaffHiltInfo : saberSingleHiltInfo;
	int i;

	trap->Cvar_VariableStringBuffer( cvar, cur, sizeof( cur ) );
	for ( i = 0; i < MAX_SABER_HILTS && info[i]; i++ ) {
		if ( !Q_stricmp( info[i], cur ) ) return i;
	}
	return -1;
}

static int LK_BladeIndex( const char *cvar ) {
	char cur[32];
	int i;

	trap->Cvar_VariableStringBuffer( cvar, cur, sizeof( cur ) );
	for ( i = 0; i < 6; i++ ) {
		if ( !Q_stricmp( cur, lkBladeNames[i] ) ) return i;
	}
	return 4;
}

// the single saber's stance the stage shows: the one picked with Space, else Medium, within the Saber Attack rank
static int LK_Stance( void ) {
	const int attack = uiForcePowersRank[FP_SABER_OFFENSE];
	const int most = attack < 1 ? 0 : attack > 3 ? 2 : attack - 1;
	const int st = lk.stance < 0 ? 1 : lk.stance;
	return st > most ? most : st;
}

static void LK_ModelSaber( void ) {
	const int type = LK_SaberType();
	const int feeder = type == 2 ? FEEDER_SABER_STAFF_INFO : FEEDER_SABER_SINGLE_INFO;
	const int count = uiInfo.uiDC.feederCount( feeder );
	const int row1 = LK_HiltRow( type, "ui_saber" ), row2 = type == 1 ? LK_HiltRow( type, "ui_saber2" ) : -1;
	const int attack = uiForcePowersRank[FP_SABER_OFFENSE];
	lkHead_t *h;
	lkField_t *f;
	int i;

	h = LK_AddHead( LKHD_SEG, LKHA_TYPE, "TYPE" );
	LK_AddOpt( h, "Single" ); LK_AddOpt( h, "Dual" ); LK_AddOpt( h, "Staff" );
	h->cur = type;
	if ( type == 1 ) {
		h = LK_AddHead( LKHD_SEG, LKHA_HAND, "LIST SETS" );
		LK_AddOpt( h, "Hilt 1" ); LK_AddOpt( h, "Hilt 2" );
		h->cur = lk.hand;
	}
	h = LK_AddHead( LKHD_TEXT, 0, "" );
	Com_sprintf( h->txt, sizeof( h->txt ), "%d hilts", count );

	lkm.cur = type == 1 && lk.hand ? row2 : row1;
	for ( i = 0; i < count; i++ ) {
		qhandle_t h1, h2, h3;
		const char *name = uiInfo.uiDC.feederItemText( feeder, i, 0, &h1, &h2, &h3 );
		lkRow_t *r = LK_AddRow( name ? name : "", i );
		if ( !r ) break;
		if ( type == 1 ) {
			if ( i == row1 ) LK_AddTag( r, "HILT 1", LKS_V );
			if ( i == row2 ) LK_AddTag( r, "HILT 2", LKS_V );
		} else if ( i == row1 ) {
			LK_AddTag( r, "EQUIPPED", LKS_V );
		}
	}

	f = LK_AddField( type == 1 ? "BLADE 1" : "BLADE", LKF_BLADE1, LKA_BLADE1 );
	if ( type == 1 ) {
		LK_AddField( "BLADE 2", LKF_BLADE2, LKA_BLADE2 );
	}
	f = LK_AddField( "STANCES", LKF_STATIC, LKA_NONE );
	if ( type == 0 ) {
		static const char *stances[] = { "none", "Fast", "Fast " XP_DOT " Medium", "Fast " XP_DOT " Medium " XP_DOT " Strong" };
		Q_strncpyz( f->v, stances[attack < 0 ? 0 : attack > 3 ? 3 : attack], sizeof( f->v ) );
		if ( attack > 1 ) {
			static const char *names[] = { "Fast", "Medium", "Strong" };
			Com_sprintf( f->n, sizeof( f->n ), "shown: %s " XP_DOT " space", names[LK_Stance()] );
		} else {
			Q_strncpyz( f->n, "Saber Attack adds more", sizeof( f->n ) );
		}
	} else {
		Q_strncpyz( f->v, type == 1 ? "Dual" : "Staff", sizeof( f->v ) );
		Q_strncpyz( f->n, "server decides", sizeof( f->n ) );
	}
}

/* Force */

static const int lkForceOrder[] = {
	FP_LEVITATION, FP_SPEED, FP_PUSH, FP_PULL, FP_SEE,
	FP_HEAL, FP_TELEPATHY, FP_PROTECT, FP_ABSORB, FP_TEAM_HEAL,
	FP_GRIP, FP_LIGHTNING, FP_RAGE, FP_DRAIN, FP_TEAM_FORCE,
	FP_SABER_OFFENSE, FP_SABER_DEFENSE, FP_SABERTHROW
};
static const char *lkForceNames[NUM_FORCE_POWERS] = {
	"Heal", "Jump", "Speed", "Push", "Pull", "Mind Trick", "Grip", "Lightning", "Rage", "Protect", "Absorb", "Team Heal",
	"Energize", "Drain", "Sight", "Saber Attack", "Saber Defend", "Saber Throw"
};
static const char *lkForceItems[NUM_FORCE_POWERS] = {
	"setfp_healself", "setfp_jump", "setfp_speed", "setfp_push", "setfp_pull", "setfp_mindtrick", "setfp_grip",
	"setfp_lightning", "setfp_rage", "setfp_protect", "setfp_absorb", "setfp_teamheal", "setfp_powerother", "setfp_drain",
	"setfp_see", "setfp_saberattack", "setfp_saberdefend", "setfp_saberthrow"
};

static int LK_ForceGroup( int p ) {
	if ( p >= FP_SABER_OFFENSE ) return 3;
	return uiForcePowerDarkLight[p] == FORCE_LIGHTSIDE ? 1 : uiForcePowerDarkLight[p] == FORCE_DARKSIDE ? 2 : 0;
}

// why a power can not change (raise: why it can not go up), or NULL (the stock rules, design 3.7)
static const char *LK_PowerWhy( int p, qboolean raise ) {
	static char why[96];
	const char *no = XP_NoForceReason( &lkm.c );
	int cost;

	if ( no ) return no;
	if ( uiForcePowersDisabled[p] ) return "Disabled on this server";
	if ( uiForcePowerDarkLight[p] && uiForcePowerDarkLight[p] != uiForceSide ) {
		return uiForcePowerDarkLight[p] == FORCE_LIGHTSIDE ? "Light side only" : "Dark side only";
	}
	if ( ( p == FP_SABER_DEFENSE || p == FP_SABERTHROW ) && uiForcePowersRank[FP_SABER_OFFENSE] < 1 && !ui_freeSaber.integer ) {
		return "Needs Saber Attack 1";
	}
	if ( raise ) {
		if ( uiForcePowersRank[p] >= FORCE_LEVEL_3 ) return "Already at rank 3";
		cost = bgForcePowerCost[p][uiForcePowersRank[p] + 1];
		if ( cost > uiForceAvailable ) {
			Com_sprintf( why, sizeof( why ), "Not enough points (needs %d, %d left)", cost, uiForceAvailable );
			return why;
		}
	}
	return NULL;
}

static void LK_ModelForce( void ) {
	const char *no = XP_NoForceReason( &lkm.c );
	const qboolean team = LK_TeamMode( lkm.c.gametype );
	lkHead_t *h;
	lkField_t *f;
	int i;

	h = LK_AddHead( LKHD_SEG, LKHA_SIDE, "SIDE" );
	LK_AddOpt( h, "Light" ); LK_AddOpt( h, "Dark" );
	h->cur = uiForceSide == FORCE_DARKSIDE ? 1 : 0;
	if ( lkm.c.forceBasedTeams && ( lkm.c.team == TEAM_RED || lkm.c.team == TEAM_BLUE ) ) {
		Q_strncpyz( h->off, lkm.c.team == TEAM_RED ? "Red team is dark side on this server" : "Blue team is light side on this server",
			sizeof( h->off ) );
	} else if ( no && uiJediNonJedi != FORCE_NONJEDI ) {
		Q_strncpyz( h->off, no, sizeof( h->off ) );
	}
	if ( lkm.c.connected && UI_TrueJediEnabled() && !XP_AllForceDisabled( lkm.c.disabledForce ) ) {
		h = LK_AddHead( LKHD_SEG, LKHA_CLASS, "CLASS" );
		LK_AddOpt( h, "Jedi" ); LK_AddOpt( h, "Non-Jedi" );
		h->cur = uiJediNonJedi == FORCE_NONJEDI ? 1 : 0;
	}
	LK_AddHead( LKHD_METER, 0, "" );

	for ( i = 0; i < (int)ARRAY_LEN( lkForceOrder ); i++ ) {
		const int p = lkForceOrder[i], level = uiForcePowersRank[p];
		const char *why;
		lkRow_t *r;
		if ( ( p == FP_TEAM_HEAL || p == FP_TEAM_FORCE ) && !team ) continue;
		if ( ( r = LK_AddRow( lkForceNames[p], p ) ) == NULL ) break;
		r->group = LK_ForceGroup( p );
		why = LK_PowerWhy( p, qfalse );
		if ( why ) Q_strncpyz( r->why, why, sizeof( r->why ) );
		if ( no ) continue;
		r->pips = level;
		if ( level >= FORCE_LEVEL_3 ) {
			LK_AddTag( r, "MAX", LKS_V );
		} else if ( !why ) {	// a power that can not be bought shows why instead of its cost
			const int cost = bgForcePowerCost[p][level + 1];
			LK_AddTag( r, va( "+%d", cost ), cost > uiForceAvailable ? LKS_COSTNO : LKS_PAPER );
		}
		if ( uiForcePowersDisabled[p] ) LK_AddTag( r, "OFF", LKS_DK );
		else if ( uiForcePowerDarkLight[p] && uiForcePowerDarkLight[p] != uiForceSide ) {
			LK_AddTag( r, uiForcePowerDarkLight[p] == FORCE_LIGHTSIDE ? "LIGHT" : "DARK", LKS_MUTE );
		} else if ( ( p == FP_SABER_DEFENSE || p == FP_SABERTHROW ) && uiForcePowersRank[FP_SABER_OFFENSE] < 1 && !ui_freeSaber.integer ) {
			LK_AddTag( r, "NEEDS ATTACK", LKS_MUTE );
		}
	}
	if ( lk.focus[XP_FORCE] >= lkm.numRows ) lk.focus[XP_FORCE] = lkm.numRows - 1;
	if ( lk.focus[XP_FORCE] < 0 ) lk.focus[XP_FORCE] = 0;
	lkm.cur = lk.focus[XP_FORCE];

	{
		const int count = uiInfo.uiDC.feederCount( FEEDER_FORCECFG );
		qhandle_t h1, h2, h3;
		const char *name;
		if ( lk.tpl >= count ) lk.tpl = 0;
		f = LK_AddField( "TEMPLATE", LKF_VALUE, LKA_TEMPLATE );
		name = count > 0 ? uiInfo.uiDC.feederItemText( FEEDER_FORCECFG, lk.tpl, 0, &h1, &h2, &h3 ) : NULL;
		Q_strncpyz( f->v, name ? name : "Custom", sizeof( f->v ) );
		Q_strncpyz( f->n, "Enter loads", sizeof( f->n ) );
		f->arrows = (qboolean)( count > 1 );
		if ( no ) Q_strncpyz( f->off, no, sizeof( f->off ) );
		f = LK_AddField( "SAVE AS", LKF_BUTTON, LKA_SAVE );
		trap->Cvar_VariableStringBuffer( "ui_SaveFCF", f->v, sizeof( f->v ) );
		if ( !f->v[0] && lk.edit != LKE_SAVE ) Q_strncpyz( f->v, "my_build", sizeof( f->v ) );
		Q_strncpyz( f->n, lk.edit == LKE_SAVE ? "Enter saves" : "Enter to name", sizeof( f->n ) );
		if ( no ) Q_strncpyz( f->off, no, sizeof( f->off ) );
		if ( !lkm.c.connected ) {
			f = LK_AddField( "NOTE", LKF_STATIC, LKA_NONE );
			Q_strncpyz( f->v, "Each server trims this build", sizeof( f->v ) );
		}
	}
}

/* Team */

static void LK_Players( int counts[TEAM_NUM_TEAMS], char names[TEAM_NUM_TEAMS][96] ) {
	int i;

	memset( counts, 0, sizeof( int ) * TEAM_NUM_TEAMS );
	for ( i = 0; i < TEAM_NUM_TEAMS; i++ ) names[i][0] = 0;
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		char info[MAX_INFO_STRING], name[MAX_NETNAME];
		int t;
		trap->GetConfigString( CS_PLAYERS + i, info, sizeof( info ) );
		if ( !info[0] ) continue;
		t = atoi( Info_ValueForKey( info, "t" ) );
		if ( t < 0 || t >= TEAM_NUM_TEAMS ) continue;
		counts[t]++;
		Q_strncpyz( name, Info_ValueForKey( info, "n" ), sizeof( name ) );
		Q_StripColor( name );
		if ( counts[t] <= 4 ) {
			Q_strcat( names[t], 96, va( "%s%s", names[t][0] ? " " XP_DOT " " : "", name ) );
		}
	}
}

static void LK_Card( const char *word, const char *what, const char *tag, int style, const char *pl, const char *why ) {
	lkRow_t *r = LK_AddRow( word, lkm.numRows );

	if ( !r ) return;
	Q_strncpyz( r->what, what, sizeof( r->what ) );
	if ( tag && tag[0] ) LK_AddTag( r, tag, style );
	if ( pl ) Q_strncpyz( r->pl, pl, sizeof( r->pl ) );
	if ( why ) Q_strncpyz( r->why, why, sizeof( r->why ) );
}

static void LK_ModelTeam( void ) {
	const xpContext_t *c = &lkm.c;
	const int g = c->gametype;
	int counts[TEAM_NUM_TEAMS];
	char names[TEAM_NUM_TEAMS][96], map[MAX_INFO_STRING], info[MAX_INFO_STRING];
	const char *spec = c->team == TEAM_SPECTATOR ? "You are watching" : NULL;
	lkHead_t *h = LK_AddHead( LKHD_TEXT, 0, "" );
	lkField_t *f;

	if ( !c->connected ) {
		Q_strncpyz( h->txt, "Offline " XP_DOT " connect to choose a team", sizeof( h->txt ) );
		LK_Card( "Offline", "", "main menu", LKS_MUTE, NULL, "Nothing to join until you connect" );
		f = LK_AddField( "NOTE", LKF_STATIC, LKA_NONE );
		Q_strncpyz( f->v, "Connect to choose a team", sizeof( f->v ) );
		lkm.cur = lk.focus[XP_TEAM] = 0;
		return;
	}
	LK_Players( counts, names );
	trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
	Q_strncpyz( map, Info_ValueForKey( info, "mapname" ), sizeof( map ) );
	if ( LK_TeamMode( g ) ) {
		Com_sprintf( h->txt, sizeof( h->txt ), "%s " XP_DOT " %s " XP_DOT " %d v %d", XP_GameTypeName( g ), map, counts[TEAM_RED], counts[TEAM_BLUE] );
		LK_Card( "Red", "red", va( "%d", counts[TEAM_RED] ), LKS_RED, names[TEAM_RED], c->team == TEAM_RED ? "You are on red" : NULL );
		LK_Card( "Blue", "blue", va( "%d", counts[TEAM_BLUE] ), LKS_BLUE, names[TEAM_BLUE], c->team == TEAM_BLUE ? "You are on blue" : NULL );
		LK_Card( "Auto", "auto", "server picks", LKS_MUTE, NULL, c->team != TEAM_SPECTATOR ? "Auto is for joining from spectator" : NULL );
		LK_Card( "Spectate", "s", "watch", LKS_MUTE, NULL, spec );
	} else {
		Com_sprintf( h->txt, sizeof( h->txt ), "%s " XP_DOT " %s", XP_GameTypeName( g ), map );
		if ( g == GT_SIEGE ) {
			LK_Card( "Choose Class", "class", "siege", LKS_MUTE, NULL, NULL );
		} else if ( g == GT_POWERDUEL ) {
			LK_Card( "Single", "single", "1 v 2", LKS_MUTE, NULL, NULL );
			LK_Card( "Double", "double", "2 v 1", LKS_MUTE, NULL, NULL );
		} else {
			LK_Card( "Join Game", "free", va( "%d playing", counts[TEAM_FREE] ), LKS_MUTE, NULL, c->team == TEAM_FREE ? "You are playing" : NULL );
		}
		LK_Card( "Spectate", "s", "watch", LKS_MUTE, NULL, spec );
	}
	if ( lk.focus[XP_TEAM] >= lkm.numRows ) lk.focus[XP_TEAM] = 0;
	lkm.cur = lk.focus[XP_TEAM];

	{
		const lkRow_t *r = &lkm.rows[lkm.cur];
		const qboolean colour = (qboolean)( !Q_stricmp( r->what, "red" ) || !Q_stricmp( r->what, "blue" ) );
		f = LK_AddField( "YOU APPEAR AS", LKF_STATIC, LKA_NONE );
		Q_strncpyz( f->v, !Q_stricmp( r->what, "red" ) ? "Red skin" : !Q_stricmp( r->what, "blue" ) ? "Blue skin" : "Your outfit", sizeof( f->v ) );
		if ( colour ) Q_strncpyz( f->n, "team skin or tint", sizeof( f->n ) );
		if ( c->forceBasedTeams && colour ) {
			f = LK_AddField( "FORCE SIDE", LKF_STATIC, LKA_NONE );
			Q_strncpyz( f->v, !Q_stricmp( r->what, "red" ) ? "Dark" : "Light", sizeof( f->v ) );
			Q_strncpyz( f->n, "set by the team", sizeof( f->n ) );
		}
		f = LK_AddField( "APPLIES", LKF_STATIC, LKA_NONE );
		Q_strncpyz( f->v, "Model now", sizeof( f->v ) );
		Q_strncpyz( f->n, "saber and Force at spawn", sizeof( f->n ) );
		if ( g == GT_POWERDUEL ) {
			f = LK_AddField( "POWER DUEL", LKF_STATIC, LKA_NONE );
			Q_strncpyz( f->v, "A preference", sizeof( f->v ) );
			Q_strncpyz( f->n, "the server assigns", sizeof( f->n ) );
		}
	}
}

/* the commit row (design 3.6) */
static void LK_ModelButtons( void ) {
	const xpContext_t *c = &lkm.c;
	const int g = c->gametype;

	if ( !c->connected ) {
		LK_AddButton( "Apply", "apply", qtrue, NULL );
		LK_AddButton( "Back", "back", qfalse, NULL );
		return;
	}
	if ( g == GT_SIEGE ) {
		LK_AddButton( "Choose Class", "class", qtrue, NULL );
		LK_AddButton( "Spectate", "s", qfalse, NULL );
		return;
	}
	if ( g == GT_POWERDUEL ) {
		LK_AddButton( "Join Single", "single", qtrue, NULL );
		LK_AddButton( "Join Double", "double", qfalse, NULL );
		LK_AddButton( "Spectate", "s", qfalse, NULL );
		return;
	}
	if ( c->team == TEAM_SPECTATOR ) {
		if ( LK_TeamMode( g ) ) {
			LK_AddButton( "Auto", "auto", qtrue, NULL );
			LK_AddButton( "Join Red", "red", qfalse, lkRed );
			LK_AddButton( "Join Blue", "blue", qfalse, lkBlue );
		} else {
			LK_AddButton( "Join Game", "free", qtrue, NULL );
		}
		LK_AddButton( "Spectate", "s", qfalse, NULL );
		return;
	}
	LK_AddButton( "Apply", "apply", qtrue, NULL );
	if ( LK_TeamMode( g ) ) {
		if ( c->team == TEAM_RED ) LK_AddButton( "Join Blue", "blue", qfalse, lkBlue );
		else LK_AddButton( "Join Red", "red", qfalse, lkRed );
	}
	LK_AddButton( "Spectate", "s", qfalse, NULL );
}

static void LK_Model( void ) {
	memset( &lkm, 0, sizeof( lkm ) );
	XP_Context( &lkm.c );
	lkm.sec = XP_Section();
	if ( lkm.sec != lk.lastSection ) {
		// a section opens on its list; Saber on its type switch (the type shapes the list: design 3.4)
		lk.lastSection = lkm.sec;
		lk.zone = lkm.sec == XP_SABER ? LKZ_HEAD : LKZ_LIST;
		lk.hf[lkm.sec] = 0;
		lk.edit = LKE_NONE;
	}
	switch ( lkm.sec ) {
	case XP_CHARACTER: LK_ModelCharacter(); break;
	case XP_SABER: LK_ModelSaber(); break;
	case XP_FORCE: LK_ModelForce(); break;
	default: LK_ModelTeam(); break;
	}
	LK_ModelButtons();
	if ( lk.bf >= lkm.numButtons ) lk.bf = 0;
}

static qboolean LK_HeadFocusable( int i ) {
	return (qboolean)( i >= 0 && i < lkm.numHead && lkm.head[i].kind == LKHD_SEG );
}

static qboolean LK_FieldFocusable( int i ) {
	return (qboolean)( i >= 0 && i < lkm.numFields && lkm.fields[i].kind != LKF_STATIC );
}

static qboolean LK_ZoneOk( int z ) {
	int i;

	switch ( z ) {
	case LKZ_HEAD:
		for ( i = 0; i < lkm.numHead; i++ ) if ( LK_HeadFocusable( i ) ) return qtrue;
		return qfalse;
	case LKZ_LIST:
		return (qboolean)( lkm.numRows > 0 );
	case LKZ_DETAIL:
		for ( i = 0; i < lkm.numFields; i++ ) if ( LK_FieldFocusable( i ) ) return qtrue;
		return qfalse;
	default:
		return (qboolean)( lkm.numButtons > 0 );
	}
}

// keeps the head and detail focus on something focusable
static void LK_FixFocus( void ) {
	const int s = lkm.sec;
	int i;

	if ( !LK_HeadFocusable( lk.hf[s] ) ) {
		for ( i = 0; i < lkm.numHead && !LK_HeadFocusable( i ); i++ );
		lk.hf[s] = i < lkm.numHead ? i : 0;
	}
	if ( !LK_FieldFocusable( lk.df[s] ) ) {
		for ( i = 0; i < lkm.numFields && !LK_FieldFocusable( i ); i++ );
		lk.df[s] = i < lkm.numFields ? i : 0;
	}
	if ( !LK_ZoneOk( lk.zone ) ) {
		lk.zone = LKZ_LIST;
	}
}

/* ------------------------------------------------------------------ painting */

static void LK_Hover( void );

static void LK_PaintName( void ) {
	char name[MAX_NETNAME];
	const qboolean focus = (qboolean)( lk.edit == LKE_NAME ), hover = LK_Hovered( LKH_NAME, 0, -1 );
	const float top = 44, size = 36, base = LK_Base( LK_DISP, top, size, 1.1f );
	vec4_t c, line;
	float x, w;

	// label "NAME" on the input's baseline
	{
		const float lsize = 15, ltop = base - ( LK_Base( LK_TAG, 0, lsize, 0 ) );
		LK_Text( LK_TAG, 64, ltop, lsize, 0, 0.08f, -1, lkSec, "NAME", 0 );
	}
	x = 64 + LK_Width( LK_TAG, 15, 0.08f, "NAME", 0 ) + 14;
	trap->Cvar_VariableStringBuffer( "ui_Name", name, sizeof( name ) );
	LK_Col( c, hover && !focus ? lkAcc : lkPaper, 1 );
	w = LK_Text( LK_DISP, x + 6, top, size, 1.1f, 0, -1, c, name, LKT_UPPER | LKT_SHADOW );
	if ( focus && ( ( uiInfo.uiDC.realTime - lk.editStart ) / 500 ) % 2 == 0 ) {
		LK_Para( x + 8 + w, top + 6, 3, 30, LK_TAN12, lkAcc );
	}
	LK_Col( line, focus ? lkAcc : lkPaper, focus ? 1.0f : 0.35f );
	LK_Rect( x, top + 39.6f, 330, 2, line );
	LK_Hit( x, top, 330, 42, LKH_NAME, 0, 0 );
}

static void LK_PaintStrip( void ) {
	char info[MAX_INFO_STRING];
	const int rank = uiForceRank >= 0 && uiForceRank < NUM_FORCE_MASTERY_LEVELS ? uiForceRank : 0;
	float x = 64;
	int i, off = 0;

	if ( !lkm.c.connected ) {
		x += LK_TagDraw( x, 104, "MAIN MENU", LKS_MUTE, 1 ) + 8;
	} else {
		trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
		x += LK_TagDraw( x, 104, XP_GameTypeName( lkm.c.gametype ), LKS_V, 1 ) + 8;
		x += LK_TagDraw( x, 104, Info_ValueForKey( info, "mapname" ), LKS_MUTE, 1 ) + 8;
	}
	x += LK_TagDraw( x, 104, va( "%s " XP_DOT " %d PTS", XP_RankName( rank ), forceMasteryPoints[rank] ), LKS_MUTE, 1 ) + 8;
	if ( lkm.c.connected ) {
		if ( lkm.c.forceBasedTeams ) {
			x += LK_TagDraw( x, 104, "FORCE TEAMS", LKS_DK, 1 ) + 8;
		}
		for ( i = 0; i < NUM_FORCE_POWERS; i++ ) {
			if ( lkm.c.disabledForce & ( 1 << i ) ) off++;
		}
		if ( off && off < NUM_FORCE_POWERS ) {
			LK_TagDraw( x, 104, va( "%d POWER%s OFF", off, off == 1 ? "" : "S" ), LKS_DK, 1 );
		}
	}
}

// the stage's tag for UI_PickerStageState: 1 (the dwell before a load starts, the icon shown) reads as loading too
static const char *LK_StageTag( int state ) {
	switch ( state ) {
	case 0: return "LIVE MODEL";
	case 3: return "PREVIEW LIMIT";
	case 4: return "NOT LOADED";
	default: return "LOADING";
	}
}

static void LK_PaintCharName( void ) {
	int character, variant, team, state;
	vec4_t c;
	float x = 72;

	UI_PickerGet( &character, &variant, &team );
	if ( character < 0 ) {
		return;
	}
	LK_Col( c, lkPaper, 1 );
	LK_Text( LK_DISP, 64, 846, 72, 0.9f, 0, -1, c, uiRoster.characters[character].name, LKT_UPPER | LKT_SHADOW );
	state = UI_PickerStageState();
	x += LK_TagDraw( x, 922, uiRoster.variants[uiRoster.characters[character].firstVariant + variant].name, LKS_V, 1 ) + 8;
	LK_TagDraw( x, 922, LK_StageTag( state ),
		state >= 3 ? LKS_DK : LKS_MUTE, 1 );
}

static void LK_PaintTabs( void ) {
	static const char *words[XP_NUM_SECTIONS] = { "CHARACTER", "SABER", "FORCE", "TEAM" };
	int i;

	for ( i = 0; i < XP_NUM_SECTIONS; i++ ) {
		const float X = 820 + i * 258.0f, Y = 62;
		const qboolean cur = (qboolean)( i == lkm.sec ), hover = LK_Hovered( LKH_TAB, i, -1 );
		const float s = cur ? 1.3f : 1.0f, size = 40 * s;
		const float tw = LK_Width( LK_DISP, size, 0, words[i], 0 );
		const float boxW = tw + 20 * s, boxH = 48 * s, top = Y + 48 - boxH;
		char summary[128];
		vec4_t c;
		if ( i == XP_TEAM && !lkm.c.connected ) continue;
		if ( cur ) {
			LK_Brush( X - 8 * s, top - 8 * s, boxW + 24 * s, boxH + 14 * s, qfalse, 1 );
			LK_Col( c, lkInk, 1 );
		} else {
			LK_Col( c, lkPaper, hover ? 1.0f : 0.72f );
		}
		LK_Text( LK_DISP, X + 8 * s, top + 6 * s, size, 1.0f, 0, top + boxH * 0.5f, c, words[i], cur ? 0 : LKT_SHADOW );
		XP_Summary( i, summary, sizeof( summary ) );
		while ( summary[0] && LK_Width( LK_TAG, 18, 0, summary, 0 ) > 238 ) {
			summary[strlen( summary ) - 1] = 0;
		}
		LK_Col( c, lkPaper, cur ? 1.0f : 0.6f );
		LK_Text( LK_TAG, X + 10, Y + 66, 18, 1.2f, 0, -1, c, summary, 0 );
		LK_Hit( X - 8, Y - 8, 250, 100, LKH_TAB, i, 0 );
	}
}

// the head row: from (820, 190), items centred on y 216, 36 apart
static void LK_PaintHead( void ) {
	const float mid = 216;
	const qboolean active = (qboolean)( lk.zone == LKZ_HEAD );
	float x = 820;
	int i, k;

	for ( i = 0; i < lkm.numHead; i++ ) {
		const lkHead_t *h = &lkm.head[i];
		if ( h->kind == LKHD_SEG ) {
			const qboolean on = (qboolean)( active && i == lk.hf[lkm.sec] );
			const float kw = LK_Width( LK_TAG, 15, 0.08f, h->k, 0 ), lh = LK_Line( LK_TAG, 15, 0 ) + 9;
			const float alpha = h->off[0] ? 0.5f : 1.0f;
			vec4_t c;
			float sx;
			if ( on ) {
				LK_Brush( x - 8, mid - lh * 0.5f - 7, kw + 14 + 20, lh + 14, qfalse, 1 );
			}
			LK_Text( LK_TAG, x + 6, mid - lh * 0.5f + 5, 15, 0, 0.08f, mid, on ? lkInk : lkSec, h->k, 0 );
			sx = x + kw + 14 + 12;
			for ( k = 0; k < h->numOpts; k++ ) {
				const float ow = LK_Width( LK_TAG, 17, 0.04f, h->opts[k], LKT_UPPER ) + 28;
				const qboolean sel = (qboolean)( k == h->cur ), hover = LK_Hovered( LKH_HEAD, i, k );
				vec4_t bg, fg, ring;
				if ( sel ) {
					LK_Col( bg, lkAcc, alpha );
					LK_Col( fg, lkInk, alpha );
					if ( on ) {
						LK_Col( ring, lkPaper, 1 );
						LK_Para( sx - 2, mid - 19, ow + 4, 38, LK_TAN12, ring );
					}
					LK_Para( sx, mid - 17, ow, 34, LK_TAN12, bg );
				} else {
					LK_Col( ring, lkSec, 0.5f * alpha );
					LK_Col( bg, hover ? lkSec : lkInk, hover ? 0.25f * alpha : 0.6f * alpha );
					LK_Col( fg, lkPaper, 0.85f * alpha );
					LK_ParaRing( sx, mid - 17, ow, 34, LK_TAN12, 1.5f, ring, bg );
				}
				LK_Text( LK_TAG, sx + 14, mid - 17 + 9, 17, 1.0f, 0.04f, mid, fg, h->opts[k], LKT_UPPER );
				LK_Hit( sx - 4, mid - 20, ow + 6, 40, LKH_HEAD, i, k );
				sx += ow + 2;
			}
			if ( h->off[0] ) {
				LK_Col( c, lkWarn, 1 );
				sx += 12;
				sx += LK_Text( LK_TAG, sx, mid - 9, 14, 0, 0, mid, c, h->off, LKT_UPPER );
			}
			x = sx + 36;
		} else if ( h->kind == LKHD_TEXT ) {
			vec4_t c;
			LK_Col( c, lkPaper, 0.7f );
			x += LK_Text( LK_TAG, x, mid - LK_Line( LK_TAG, 18, 0 ) * 0.5f, 18, 0, 0, mid, c, h->txt, LKT_UPPER ) + 36;
		} else {
			// the Force budget: rank, the bar (used, and the focused row's next cost as a preview), points left
			const char *no = XP_NoForceReason( &lkm.c );
			const int rank = uiForceRank >= 0 && uiForceRank < NUM_FORCE_MASTERY_LEVELS ? uiForceRank : 0;
			const int total = forceMasteryPoints[rank], used = total - uiForceAvailable;
			const char *rn = XP_RankName( rank );
			vec4_t c;
			float bx;
			x += LK_Text( LK_TAG, x + 6, mid - LK_Line( LK_TAG, 15, 0 ) * 0.5f, 15, 0, 0.08f, mid, lkSec, rn, LKT_UPPER ) + 6 + 14;
			LK_Col( c, lkPaper, 0.14f );
			LK_Para( x, mid - 7, 300, 14, LK_TAN12, c );
			bx = x;
			if ( !no && total > 0 ) {
				const float uw = 300.0f * used / total;
				if ( uw > 0 ) LK_Para( bx, mid - 7, uw, 14, LK_TAN12, lkAcc );
				if ( lk.zone == LKZ_LIST && lkm.cur >= 0 && lkm.cur < lkm.numRows ) {
					const int p = lkm.rows[lkm.cur].id;
					if ( uiForcePowersRank[p] < FORCE_LEVEL_3 && !lkm.rows[lkm.cur].why[0] ) {
						const int cost = bgForcePowerCost[p][uiForcePowersRank[p] + 1];
						const float pw = 300.0f * ( cost < total - used ? cost : total - used ) / total;
						LK_Col( c, cost > uiForceAvailable ? lkWarn : lkPaper, cost > uiForceAvailable ? 1.0f : 0.6f );
						if ( pw > 0 ) LK_Para( bx + uw, mid - 7, pw, 14, LK_TAN12, c );
					}
				}
			}
			x += 300 + 14;
			LK_Col( c, lkPaper, no ? 0.5f : 1.0f );
			x += LK_Text( LK_DISP, x, mid - 16, 32, 1.0f, 0, mid, c, no ? "-" : va( "%d", uiForceAvailable ), 0 ) + 8;
			LK_Text( LK_TAG, x, mid - 2, 15, 0, 0, mid, lkSec, no ? "locked" : va( "of %d left", total ), 0 );
		}
	}
}

typedef struct lkListCfg_s {
	float	x0, y0, step, big, s, sSel, gap;
	int		win, lead;
} lkListCfg_t;

static const lkListCfg_t lkLists[XP_NUM_SECTIONS] = {
	{ 840, 318, 40, 26, 0.5625f, 0.875f, 14, 15, 6 },
	{ 840, 318, 44, 26, 0.5625f, 0.875f, 14, 12, 4 },
	{ 840, 340, 36, 14, 0.47f, 0.66f, 12, 99, 0 },
	{ 840, 330, 76, 30, 0.69f, 1.0f, 16, 6, 2 },
};

#define LK_CLEAR_SLOPE	0.1087f		// the stage's keep-clear line: x falls this much per y down the screen
#define LK_CLEAR_MARGIN	30.0f
#define LK_LIST_RIGHT	1436.0f		// the detail column starts at 1440: tags that would cross it are left out

// a row's tags (and the Force pips) from x, the row's bottom at y; tags sit 8 above it
static void LK_RowTags( const lkRow_t *r, float x, float bottom, float alpha ) {
	int k;

	if ( r->pips >= 0 ) {
		x += LK_Pips( x, bottom - 8 - 12, r->pips, alpha ) + 8;
	}
	for ( k = 0; k < r->numTags; k++ ) {
		if ( x + LK_Width( LK_TAG, 15, 0.04f, r->tags[k].t, LKT_UPPER ) + 18 > LK_LIST_RIGHT ) {
			break;
		}
		x += LK_TagDraw( x, bottom - 8 - 24, r->tags[k].t, r->tags[k].style, alpha ) + 8;
	}
	if ( r->pl[0] ) {
		vec4_t c;
		LK_Col( c, lkPaper, 0.75f * alpha );
		LK_Text( LK_TAGU, x + 6, bottom - 8 - 22, 17, 0, 0, -1, c, r->pl, 0 );
	}
}

static void LK_PaintRow( int i, float x, float y, const lkListCfg_t *cfg, qboolean active ) {
	const lkRow_t *r = &lkm.rows[i];
	const qboolean cur = (qboolean)( i == lkm.cur ), off = (qboolean)( r->why[0] != 0 );
	const qboolean hover = LK_Hovered( LKH_LIST, i, -1 );
	const float s = cur ? cfg->sSel : cfg->s;
	float alpha = cur ? 1.0f : active ? 0.92f : 0.8f, w;

	if ( off ) {
		alpha *= cur ? 0.7f : 0.4f;
	}
	w = LK_Word( x, y, s, r->word, cur, active, alpha, hover && !cur );
	LK_RowTags( r, x + w + cfg->gap, y, off ? 0.75f : 1.0f );
	LK_Hit( x - 12, y - 63.6f * s - 4, w + 400, 63.6f * s + 8, LKH_LIST, i, 0 );
}

static void LK_PaintList( void ) {
	const lkListCfg_t *cfg = &lkLists[lkm.sec];
	const qboolean active = (qboolean)( lk.zone == LKZ_LIST );
	int i;

	if ( lkm.sec == XP_FORCE ) {
		// two columns (neutral over light, dark over saber), each a stack on the shear line with a group tag; the
		// selected row opens below itself only, so the grid never reflows across groups
		static const int columns[2][2] = { { 0, 1 }, { 2, 3 } };
		static const char *heads[4] = { "NEUTRAL", "LIGHT SIDE", "DARK SIDE", "SABER" };
		static const int headStyles[4] = { LKS_N, LKS_LT, LKS_DK, LKS_V };
		int col, gi;
		for ( col = 0; col < 2; col++ ) {
			const float cx = col ? 1180 : 840;
			float y = cfg->y0;
			for ( gi = 0; gi < 2; gi++ ) {
				const int g = columns[col][gi];
				qboolean any = qfalse;
				for ( i = 0; i < lkm.numRows; i++ ) if ( lkm.rows[i].group == g ) any = qtrue;
				if ( !any ) continue;
				LK_TagDraw( cx + ( y - cfg->y0 ) * LK_SLOPE - 2, y - 30, heads[g], headStyles[g], 1 );
				y += 14;
				for ( i = 0; i < lkm.numRows; i++ ) {
					if ( lkm.rows[i].group != g ) continue;
					y += cfg->step;
					if ( i == lkm.cur ) y += cfg->big;
					LK_PaintRow( i, cx + ( y - cfg->y0 ) * LK_SLOPE, y, cfg, active );
				}
				y += 44;
			}
		}
		return;
	}
	{
		const int n = lkm.numRows, f = lkm.cur < 0 ? 0 : lkm.cur;
		int top = n <= cfg->win ? 0 : f - cfg->lead;
		if ( top > n - cfg->win ) top = n - cfg->win;
		if ( top < 0 ) top = 0;
		for ( i = top; i < n && i < top + cfg->win; i++ ) {
			const int k = i - top;
			const float y = cfg->y0 + k * cfg->step + ( i > f ? cfg->big : 0 ) + ( i == f ? cfg->big : 0 );
			LK_PaintRow( i, cfg->x0 + ( y - cfg->y0 ) * LK_SLOPE, y, cfg, active );
		}
	}
}

static void LK_Chips( float x, float mid, int field, int cur, int count, const vec4_t *cols, const qboolean *usable, qboolean focus ) {
	int k;

	for ( k = 0; k < count; k++ ) {
		const float cx = x + 14 + k * 38.0f;
		const qboolean on = (qboolean)( k == cur ), hover = LK_Hovered( LKH_CHIP, field, k );
		const float r = on ? 16.5f : 14.0f, cy = mid - ( on ? 4.0f : hover ? 3.0f : 0.0f );
		vec4_t c;
		LK_Col( c, cols[k], usable && !usable[k] && !on ? 0.22f : 1.0f );
		if ( on ) {
			vec4_t g;
			LK_Col( g, cols[k], 0.8f );
			LK_Pic( cx - 34, cy - 34, 68, 68, lk.glow, g );
			LK_Pic( cx - r - 3, cy - r - 3, 2 * r + 6, 2 * r + 6, lk.dot, focus ? lkInk : lkPaper );
		}
		LK_Pic( cx - r, cy - r, 2 * r, 2 * r, lk.dot, c );
		LK_Hit( cx - 18, mid - 20, 36, 40, LKH_CHIP, field, k );
	}
}

// the detail panel: from (1440, 280), each field shifted along half the shear line
static void LK_PaintDetail( void ) {
	const qboolean active = (qboolean)( lk.zone == LKZ_DETAIL );
	float y = 0;
	int i;

	for ( i = 0; i < lkm.numFields; i++ ) {
		const lkField_t *f = &lkm.fields[i];
		const float x = 1440 + y * LK_TAN12 * 0.5f, top = 280 + y;
		const qboolean on = (qboolean)( active && i == lk.df[lkm.sec] ), stat = (qboolean)( f->kind == LKF_STATIC );
		const qboolean hover = LK_Hovered( LKH_FIELD, i, -1 ) || LK_Hovered( LKH_ARROW, i, -1 ) || LK_Hovered( LKH_CHIP, i, -1 );
		const float labelH = 24, fvTop = top + labelH, fvH = stat ? 34.0f : 44.0f, mid = fvTop + fvH * 0.5f;
		const float alpha = f->off[0] ? 0.5f : 1.0f;
		vec4_t c;
		float vx = x + 10;

		LK_Text( LK_TAG, x + 10, top, 15, 0, 0.08f, -1, on ? lkAcc : lkSec, f->k, 0 );
		if ( on ) {
			LK_Brush( x - 10, fvTop - 8, 416 + 26, fvH + 16, qfalse, 1 );
		}
		if ( f->kind == LKF_TEAMCHIPS || f->kind == LKF_BLADE1 || f->kind == LKF_BLADE2 ) {
			if ( f->kind == LKF_TEAMCHIPS ) {
				qboolean usable[3];
				int t, character, variant, team;
				UI_PickerGet( &character, &variant, &team );
				for ( t = 0; t < 3; t++ ) usable[t] = (qboolean)( !f->off[0] && UI_PickerTeamAvailable( t ) );
				LK_Chips( vx, mid, i, team, 3, lkTeamChips, usable, on );
			} else {
				LK_Chips( vx, mid, i, LK_BladeIndex( f->kind == LKF_BLADE1 ? "ui_saber_color" : "ui_saber2_color" ), 6, lkBlades, NULL, on );
			}
		} else {
			const float vsize = stat || f->kind == LKF_BUTTON ? 24.0f : 32.0f;
			const float vtop = mid - LK_Line( LK_DISP, vsize, 1.0f ) * 0.5f;
			const qboolean editing = (qboolean)( f->act == LKA_SAVE && lk.edit == LKE_SAVE );
			float w;
			if ( f->arrows ) {
				LK_Col( c, on ? lkInk : lkAcc, on || hover ? alpha : 0.45f * alpha );
				vx += LK_Text( LK_TAG, vx + 4, mid - 15, 26, 1.0f, 0, mid, c, "\x8b", 0 ) + 8 + 10;
				LK_Hit( x, fvTop, 40, fvH, LKH_ARROW, i, -1 );
			}
			if ( stat ) LK_Col( c, lkPaper, 0.82f );
			else LK_Col( c, on ? lkInk : hover ? lkAcc : lkPaper, alpha );
			w = LK_Text( LK_DISP, vx, vtop, vsize, 1.0f, 0, mid, c, f->v, LKT_UPPER );
			if ( editing && ( ( uiInfo.uiDC.realTime - lk.editStart ) / 500 ) % 2 == 0 ) {
				LK_Para( vx + w + 2, vtop + 2, 3, vsize, LK_TAN12, on ? lkInk : lkAcc );
			}
			if ( f->kind == LKF_BUTTON ) {
				LK_Col( c, on ? lkInk : lkAcc, alpha );
				LK_Rect( vx - 3, vtop + vsize + 3, w + 4, 2, c );
				w += LK_Text( LK_DISP, vx + w + 4, vtop, vsize, 1.0f, 0, mid, c, "\x9b", 0 ) + 4;
			}
			vx += w + 10;
			if ( f->arrows ) {
				LK_Col( c, on ? lkInk : lkAcc, on || hover ? alpha : 0.45f * alpha );
				LK_Text( LK_TAG, vx + 4, mid - 15, 26, 1.0f, 0, mid, c, "\x9b", 0 );
				LK_Hit( vx - 6, fvTop, 40, fvH, LKH_ARROW, i, 1 );
			}
		}
		if ( f->n[0] ) {
			const float nw = LK_Width( LK_TAG, 14, 0.03f, f->n, LKT_UPPER );
			if ( on ) LK_Col( c, lkInk, 0.7f );
			else LK_Col( c, lkPaper, 0.55f );
			LK_Text( LK_TAG, x + 416 - 12 - nw, mid - 9, 14, 0, 0.03f, mid, c, f->n, LKT_UPPER );
		}
		if ( !stat ) {
			LK_Hit( x - 10, top, 436, labelH + fvH, LKH_FIELD, i, 0 );
		}
		y += ( stat ? 60.0f : 76.0f );
		if ( f->off[0] ) {
			LK_Col( c, lkWarn, 1 );
			LK_Text( LK_TAG, x + 10, fvTop + fvH + 4, 14, 1.3f, 0, -1, c, f->off, LKT_UPPER );
			y += 22;
		}
	}
}

static void LK_PaintStatus( void ) {
	qboolean warn;
	const char *s = XP_StatusLine( &warn );
	const float top = 952, h = 17 * 1.3f + 13, w = LK_Width( LK_TAG, 17, 0, s, 0 ) + 31;
	vec4_t c, g;

	if ( !s[0] ) {
		return;
	}
	LK_Col( g, warn ? lkWarn : lkSec, warn ? 0.3f : 0.22f );
	LK_Pic( 820, top, w, h, lk.grad, g );
	LK_Para( 820, top, 3, h, LK_TAN12, warn ? lkWarn : lkSec );
	if ( warn ) {
		const vec4_t pink = { 1.0f, 0.725f, 0.741f, 1.0f };
		LK_Col( c, pink, 1 );
	} else {
		LK_Col( c, lkPaper, 0.85f );
	}
	LK_Text( LK_TAG, 820 + 15, top + 7, 17, 1.3f, 0, top + h * 0.5f, c, s, 0 );
}

// the commit row: right aligned to 1856, its boxes' tops at 1008, 36 apart
static void LK_PaintCommit( void ) {
	float widths[LK_MAX_BUTTONS], total = 0, x;
	int i;

	for ( i = 0; i < lkm.numButtons; i++ ) {
		widths[i] = 10 + ( lkm.buttons[i].swatch ? 24 : 0 ) + LK_Width( LK_DISP, 40, 0, lkm.buttons[i].t, LKT_UPPER ) + 14;
		total += widths[i] + ( i ? 36 : 0 );
	}
	x = 1856 - total;
	for ( i = 0; i < lkm.numButtons; i++ ) {
		const lkButton_t *b = &lkm.buttons[i];
		const qboolean on = (qboolean)( lk.zone == LKZ_COMMIT && i == lk.bf ), hover = LK_Hovered( LKH_COMMIT, i, -1 );
		const float s = on ? 1.08f : 1.0f, w = widths[i] * s, h = 50 * s, top = 1058 - h, left = x + widths[i] * 0.5f - w * 0.5f;
		float tx = left + 10 * s;
		vec4_t c;
		if ( on || b->primary ) {
			LK_Brush( left - 8 * s, top - 6 * s, w + 24 * s, h + 10 * s, (qboolean)!on, 1 );
		}
		if ( b->swatch ) {
			LK_Para( tx, top + 6 * s + ( 40 * s - 34 * s ) * 0.5f, 14 * s, 34 * s, LK_TAN12, b->swatch );
			tx += 24 * s;
		}
		if ( on || b->primary ) LK_Col( c, lkInk, 1 );
		else LK_Col( c, hover ? lkAcc : lkPaper, 1 );
		LK_Text( LK_DISP, tx, top + 6 * s, 40 * s, 1.0f, 0, top + h * 0.5f, c, b->t, LKT_UPPER | ( on || b->primary ? 0 : LKT_SHADOW ) );
		LK_Hit( left - 8, top - 6, w + 16, h + 10, LKH_COMMIT, i, 0 );
		x += widths[i] + 36;
	}
}

// the hints line (64, 1030): keys in paper boxes, words between
static void LK_PaintHints( void ) {
	const char *primary = lkm.numButtons ? lkm.buttons[0].t : "";
	const char *parts[][2] = {
		{ "Q", NULL }, { "E", "section" }, { "TAB", "next zone" }, { "\x18\x19", "move" }, { "\x1b\x1a", "change" },
		{ "ENTER", primary }, { "F", "face" }, { "ESC", "back" }
	};
	const float top = 1030, mid = top + 11;
	float x = 64;
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( parts ); i++ ) {
		const float kw = LK_Width( LK_TAG, 14, 0, parts[i][0], 0 ) + 14;
		vec4_t bg;
		LK_Col( bg, lkPaper, 0.9f );
		x += i ? 12 : 0;
		LK_Para( x, mid - 10, kw, 21, LK_TAN12, bg );
		LK_Text( LK_TAG, x + 7, mid - 10 + 3, 14, 1.0f, 0, mid, lkInk, parts[i][0], 0 );
		x += kw + 4;
		if ( parts[i][1] ) {
			x += LK_Text( LK_TAGU, x, mid - LK_Line( LK_TAGU, 15, 0 ) * 0.5f, 15, 0, 0, -1, i == 5 ? lkAcc : lkHint,
				parts[i][1], i == 5 ? LKT_UPPER : 0 );
		}
	}
}

// a power's gesture on the stage (design 3.2: the hand, not the effect), or -1
static int LK_Gesture( int p ) {
	switch ( p ) {
	case FP_HEAL: case FP_TEAM_HEAL: return BOTH_FORCEHEAL_QUICK;
	case FP_PUSH: return BOTH_FORCEPUSH;
	case FP_PULL: return BOTH_FORCEPULL;
	case FP_TELEPATHY: return BOTH_MINDTRICK1;
	case FP_GRIP: return BOTH_FORCEGRIP_HOLD;
	case FP_LIGHTNING: return BOTH_FORCELIGHTNING_HOLD;
	case FP_RAGE: return BOTH_FORCE_RAGE;
	case FP_PROTECT: return BOTH_FORCE_PROTECT;
	case FP_ABSORB: case FP_TEAM_FORCE: return BOTH_FORCE_ABSORB;
	case FP_DRAIN: return BOTH_FORCE_DRAIN;
	default: return -1;		// Jump's animation leaves the ground: the figure would float
	}
}

// the saber-ready stance for the draft's saber type
static int LK_StancePose( int type ) {
	static const int single[] = { BOTH_SABERFAST_STANCE, BOTH_STAND2, BOTH_SABERSLOW_STANCE };
	return type == 1 ? BOTH_SABERDUAL_STANCE : type == 2 ? BOTH_SABERSTAFF_STANCE : single[LK_Stance()];
}

// the stage, from the draft and the section (design 3.3; drawn before this frame's model, so from the last one):
// framing per section (tweened), the hilts in hand, the blades lit except in Character, a saber stance, a power's
// gesture, and in Team the focused card's team skin and blade colour (the cgame lights every blade in the team's
// colour in team modes, cg_players.c). The figure faces the camera, turned 15-20 degrees toward the menu on its
// right, never away from it and out of frame
static void LK_Stage( int sec ) {
	const int type = LK_SaberType();
	char saber1[MAX_QPATH], saber2[MAX_QPATH], col[MAX_QPATH];
	int color1, color2, preview = -1;

	switch ( sec ) {
	case XP_SABER: UI_PickerStageFrame( 1.4f, 0.68f, 200.0f, qfalse ); break;
	case XP_FORCE: UI_PickerStageFrame( 1.12f, 0.53f, 195.0f, qfalse ); break;
	default: UI_PickerStageFrame( 1.0f, 0.5f, 195.0f, qfalse ); break;
	}
	// the figure, its hands and blades keep left of the menu: a line from the tabs' left (800, 80) down past the list's
	// sheared left edge (716 at 900), less the blades' glow
	UI_PickerStageClear( LK_X( 800.0f + 80 * LK_CLEAR_SLOPE - LK_CLEAR_MARGIN ),
		LK_X( 800.0f - 1000 * LK_CLEAR_SLOPE - LK_CLEAR_MARGIN ) );
	UI_PickerStageCovers( qtrue );	// the backdrop hides the world: the renderer leaves it out

	trap->Cvar_VariableStringBuffer( "ui_saber", saber1, sizeof( saber1 ) );
	saber2[0] = 0;
	if ( type == 1 ) {
		trap->Cvar_VariableStringBuffer( "ui_saber2", saber2, sizeof( saber2 ) );
	}
	trap->Cvar_VariableStringBuffer( "ui_saber_color", col, sizeof( col ) );
	color1 = TranslateSaberColor( col );
	trap->Cvar_VariableStringBuffer( "ui_saber2_color", col, sizeof( col ) );
	color2 = TranslateSaberColor( col );
	if ( sec == XP_TEAM && lkm.sec == XP_TEAM && lkm.cur >= 0 && lkm.cur < lkm.numRows && LK_TeamMode( lkm.c.gametype ) ) {
		const char *what = lkm.rows[lkm.cur].what;
		if ( !Q_stricmp( what, "red" ) ) {
			preview = ROSTER_TEAM_RED;
			color1 = color2 = SABER_RED;
		} else if ( !Q_stricmp( what, "blue" ) ) {
			preview = ROSTER_TEAM_BLUE;
			color1 = color2 = SABER_BLUE;
		}
	}
	UI_PickerStageSabers( saber1, saber2, color1, color2, (qboolean)( sec != XP_CHARACTER ) );
	UI_PickerStagePreview( preview );
	UI_PickerStagePose( sec == XP_CHARACTER ? -1 : LK_StancePose( type ) );

	if ( sec != lk.stageSection ) {
		lk.stageSection = sec;
		lk.gestureFor = -1;
		UI_PickerStageGesture( -1 );
	}
	if ( sec == XP_FORCE && lkm.sec == XP_FORCE && lkm.cur >= 0 && lkm.cur < lkm.numRows && lkm.rows[lkm.cur].id != lk.gestureFor ) {
		lk.gestureFor = lkm.rows[lkm.cur].id;
		UI_PickerStageGesture( LK_Gesture( lk.gestureFor ) );
	}
}

static int		lkXen = -1;		// xenui (the end of this file): the model handle; 0 when xenui is off or unavailable, -1 before the first try
static qboolean LK_XenDraw( void );
static qboolean LK_XenUnder( int sec );

// xenui's motion (the end of this file): what happened, as codes the document's animations start from
enum { LKFX_LEFT, LKFX_RIGHT, LKFX_REJECT, LKFX_POP };
typedef struct {
	char	word[64];
	float	x, y, size;
} lkGhostRow_t;
typedef struct {
	char	k[64], v[64];
	float	x, y;
} lkGhostField_t;
static struct {
	int				sec;				// the section the motion was set for (-1: none yet)
	int				sp, dir, pressDir;	// section changes (1 and 2 in turn), the last one's direction, a pressed direction
	int				changeAt;			// realTime of the last section change
	int				cp, op, rp;			// commits, opens and refusals: 1 and 2 in turn (0 before the first)
	int				head[LK_MAX_HEAD], row[LK_MAX_ROWS], field[LK_MAX_FIELDS], button[LK_MAX_BUTTONS];	// fx codes
	float			wx, wy;				// the paint's parallax
	int				numRows, numFields, numGhostRows, numGhostFields;
	lkGhostRow_t	rows[LK_MAX_ROWS], ghostRows[LK_MAX_ROWS];			// the last frame's, and the old section's
	lkGhostField_t	fields[LK_MAX_FIELDS], ghostFields[LK_MAX_FIELDS];
} lkFx = { -1 };

// an item's next fx code: 1 + 2 kind + parity, the parity flipped so the same kind twice still changes the code
static void LK_Fx( int *code, int kind ) {
	const int parity = *code > 0 ? 1 - ( ( *code - 1 ) & 1 ) : 0;
	*code = 1 + kind * 2 + parity;
}

void UI_PlayerLookDraw( int layer ) {
	LK_Load();
	if ( layer == 0 ) {
		// the backdrop, under the stage (the section's painted world, its bloom on)
		const int sec = XP_Section();
		LK_Stage( sec );
		if ( LK_XenUnder( sec ) ) {
			return;		// the paint, drawn by xenui under the stage
		}
		if ( lk.bg[sec] ) {
			LK_Pic( 0, 0, 1920, 1080, lk.bg[sec], NULL );
		} else {
			LK_Rect( 0, 0, 1920, 1080, lkInk );
		}
		return;
	}
	LK_Model();
	LK_FixFocus();
	if ( LK_XenDraw() ) {	// the xenui document draws the model (the end of this file)
		lk.numHits = 0;
		return;
	}
	{
		const float mx = uiInfo.uiDC.cursorx * 3.0f, my = uiInfo.uiDC.cursory * 9.0f / 4.0f;
		lk.hover = LK_HitAt( mx, my );		// last frame's hits: what was drawn under the cursor
	}
	lk.numHits = 0;
	LK_PaintCharName();
	LK_PaintName();
	LK_PaintStrip();
	LK_PaintTabs();
	LK_PaintHead();
	LK_PaintList();
	LK_PaintDetail();
	LK_PaintStatus();
	LK_PaintCommit();
	LK_PaintHints();
	LK_Hover();
}

/* ------------------------------------------------------------------ input */

static void LK_Sound( const char *s ) {
	trap->S_StartLocalSound( trap->S_RegisterSound( s ), CHAN_LOCAL_SOUND );
}

static void LK_Reject( const char *why ) {
	XP_Flash( why );
	LK_Sound( "sound/interface/menuroam.wav" );
	lkFx.rp = lkFx.rp == 1 ? 2 : 1;
	switch ( lk.zone ) {	// the item that refused shakes
	case LKZ_HEAD: if ( lk.hf[lkm.sec] < LK_MAX_HEAD ) LK_Fx( &lkFx.head[lk.hf[lkm.sec]], LKFX_REJECT ); break;
	case LKZ_LIST: if ( lkm.cur >= 0 && lkm.cur < LK_MAX_ROWS ) LK_Fx( &lkFx.row[lkm.cur], LKFX_REJECT ); break;
	case LKZ_DETAIL: if ( lk.df[lkm.sec] < LK_MAX_FIELDS ) LK_Fx( &lkFx.field[lk.df[lkm.sec]], LKFX_REJECT ); break;
	default: if ( lk.bf < LK_MAX_BUTTONS ) LK_Fx( &lkFx.button[lk.bf], LKFX_REJECT ); break;
	}
}

static void LK_SelectCharacter( int row ) {
	int character, variant, team;

	if ( row < 0 || row >= lkm.numRows ) {
		return;
	}
	UI_PickerGet( &character, &variant, &team );
	UI_PickerSet( lkm.rows[row].id, lkm.rows[row].id == character ? variant : 0 );
}

static void LK_SelectHilt( int row ) {
	const int type = LK_SaberType();
	if ( row >= 0 && row < lkm.numRows ) {
		XP_SelectRow( LK_HiltList( type, type == 1 ? lk.hand : 0 ), row );
	}
}

static void LK_Raise( int p, qboolean raise ) {
	const char *why = LK_PowerWhy( p, raise );
	const int before = uiForcePowersRank[p];

	if ( !raise && !why && ( before == 0 || ( p == FP_LEVITATION && before <= 1 ) ) ) {
		why = p == FP_LEVITATION ? "Jump stays at rank 1" : "Already at rank 0";
	}
	if ( why ) {
		LK_Reject( why );
		return;
	}
	XP_ItemKey( lkForceItems[p], raise ? A_MOUSE1 : A_MOUSE2 );
	if ( uiForcePowersRank[p] == before ) {
		LK_Reject( raise ? "The server's rules keep this rank" : NULL );
	} else {
		int r;
		for ( r = 0; r < lkm.numRows && r < LK_MAX_ROWS; r++ ) {
			if ( lkm.rows[r].id == p ) LK_Fx( &lkFx.row[r], LKFX_POP );	// the pips pop
		}
	}
}

// the head switch i to option v
static void LK_HeadSet( int i, int v ) {
	lkHead_t *h;

	if ( i < 0 || i >= lkm.numHead || lkm.head[i].kind != LKHD_SEG ) {
		return;
	}
	h = &lkm.head[i];
	v = ( v % h->numOpts + h->numOpts ) % h->numOpts;
	if ( h->off[0] ) {
		LK_Reject( h->off );
		return;
	}
	if ( v == h->cur ) {
		return;
	}
	if ( i < LK_MAX_HEAD ) {
		LK_Fx( &lkFx.head[i], v > h->cur ? LKFX_RIGHT : LKFX_LEFT );	// the switch slides from that side
	}
	switch ( h->act ) {
	case LKHA_SOURCE:
		lk.filter = v;
		LK_Model();
		if ( lkm.cur < 0 && lkm.numRows ) {
			LK_SelectCharacter( 0 );	// the selection is not in the filter: its first character
		}
		break;
	case LKHA_TYPE: {
		static const char *items[3] = { "typebut_single", "typebut_dual", "typebut_staff" };
		XP_PressItem( items[v] );
		lk.hand = 0;
		break;
	}
	case LKHA_HAND:
		lk.hand = v;
		break;
	case LKHA_SIDE:
		XP_ItemKey( "setside", A_MOUSE1 );		// two sides: a press toggles
		lk.tpl = 0;
		break;
	case LKHA_CLASS:
		XP_ItemKey( "jedinonjedi", A_MOUSE1 );
		break;
	}
}

static void LK_FieldChange( int i, int d ) {
	const lkField_t *f;
	int character, variant, team;

	if ( i < 0 || i >= lkm.numFields ) {
		return;
	}
	f = &lkm.fields[i];
	if ( f->kind == LKF_STATIC ) {
		return;
	}
	if ( i < LK_MAX_FIELDS ) {
		LK_Fx( &lkFx.field[i], d > 0 ? LKFX_RIGHT : LKFX_LEFT );	// the value slides from that side
	}
	if ( f->off[0] ) {
		LK_Reject( f->off );
		return;
	}
	UI_PickerGet( &character, &variant, &team );
	switch ( f->act ) {
	case LKA_OUTFIT:
		UI_PickerSet( character, variant + d );
		break;
	case LKA_TEAMCOL: {
		int t, k;
		for ( k = 1; k <= 3; k++ ) {
			t = ( ( team + d * k ) % 3 + 3 ) % 3;
			if ( UI_PickerTeamAvailable( t ) ) {
				UI_PickerSetTeam( t );
				break;
			}
		}
		break;
	}
	case LKA_FACE:
		UI_PickerToggleFace();
		break;
	case LKA_BLADE1:
	case LKA_BLADE2: {
		const int cur = LK_BladeIndex( f->act == LKA_BLADE1 ? "ui_saber_color" : "ui_saber2_color" );
		const int n = ( ( cur + d ) % 6 + 6 ) % 6;
		XP_PressItem( va( "%sicon%s", lkBladeNames[n], f->act == LKA_BLADE2 ? "2" : "" ) );
		break;
	}
	case LKA_TEMPLATE: {
		const int count = uiInfo.uiDC.feederCount( FEEDER_FORCECFG );
		if ( count > 0 ) lk.tpl = ( ( lk.tpl + d ) % count + count ) % count;
		break;
	}
	default:
		break;
	}
}

static void LK_FieldPress( int i ) {
	const lkField_t *f;

	if ( i < 0 || i >= lkm.numFields ) {
		return;
	}
	f = &lkm.fields[i];
	if ( f->off[0] ) {
		LK_Reject( f->off );
		return;
	}
	switch ( f->act ) {
	case LKA_CLASSIC:
		XP_Classic();
		break;
	case LKA_TEMPLATE:
		XP_SelectRow( "fcflist", lk.tpl );
		LK_Sound( "sound/interface/button1.wav" );
		break;
	case LKA_SAVE:
		if ( lk.edit == LKE_SAVE ) {
			lk.edit = LKE_NONE;
			XP_PressItem( "templatesave" );
		} else {
			lk.edit = LKE_SAVE;
			lk.editStart = uiInfo.uiDC.realTime;
		}
		break;
	default:
		LK_FieldChange( i, 1 );
		break;
	}
}

static void LK_CommitPress( int i ) {
	if ( i < 0 || i >= lkm.numButtons ) {
		return;
	}
	LK_Sound( "sound/interface/button1.wav" );
	if ( i < LK_MAX_BUTTONS ) {
		LK_Fx( &lkFx.button[i], LKFX_POP );	// the word gives under the press
	}
	if ( !Q_stricmp( lkm.buttons[i].what, "back" ) ) {
		XP_Back();
	} else {
		lkFx.cp = lkFx.cp == 1 ? 2 : 1;	// the confirm bloom
		XP_Commit( lkm.buttons[i].what );
	}
}

static void LK_ListMove( int d ) {
	const int n = lkm.numRows;
	int f = lkm.cur < 0 ? 0 : lkm.cur + d;

	if ( n <= 0 ) {
		return;
	}
	if ( f < 0 ) f = 0;
	if ( f > n - 1 ) f = n - 1;
	switch ( lkm.sec ) {
	case XP_CHARACTER: LK_SelectCharacter( f ); break;
	case XP_SABER: LK_SelectHilt( f ); break;
	default: lk.focus[lkm.sec] = f; break;
	}
}

static void LK_ListPress( int row ) {
	const lkRow_t *r;

	if ( row < 0 || row >= lkm.numRows ) {
		return;
	}
	r = &lkm.rows[row];
	switch ( lkm.sec ) {
	case XP_CHARACTER:
		LK_SelectCharacter( row );
		break;
	case XP_SABER:
		LK_SelectHilt( row );
		break;
	case XP_FORCE:
		lk.focus[XP_FORCE] = row;
		LK_Raise( r->id, qtrue );
		break;
	default:
		lk.focus[XP_TEAM] = row;
		if ( r->why[0] ) {
			LK_Reject( r->why );
		} else if ( r->what[0] ) {
			LK_Sound( "sound/interface/button1.wav" );
			lkFx.cp = lkFx.cp == 1 ? 2 : 1;
			XP_Commit( r->what );
		}
		break;
	}
}

// moves the focus inside the zone
static void LK_Move( int d ) {
	int i, n;

	switch ( lk.zone ) {
	case LKZ_HEAD:
		for ( i = lk.hf[lkm.sec] + d, n = 0; n < lkm.numHead; n++, i += d ) {
			i = ( i % lkm.numHead + lkm.numHead ) % lkm.numHead;
			if ( LK_HeadFocusable( i ) ) { lk.hf[lkm.sec] = i; break; }
		}
		break;
	case LKZ_LIST:
		LK_ListMove( d );
		break;
	case LKZ_DETAIL:
		for ( i = lk.df[lkm.sec] + d, n = 0; n < lkm.numFields; n++, i += d ) {
			i = ( i % lkm.numFields + lkm.numFields ) % lkm.numFields;
			if ( LK_FieldFocusable( i ) ) { lk.df[lkm.sec] = i; break; }
		}
		break;
	default:
		if ( lkm.numButtons ) lk.bf = ( ( lk.bf + d ) % lkm.numButtons + lkm.numButtons ) % lkm.numButtons;
		break;
	}
}

// Left / Right: the focused value
static void LK_Change( int d ) {
	switch ( lk.zone ) {
	case LKZ_HEAD:
		LK_HeadSet( lk.hf[lkm.sec], lkm.head[lk.hf[lkm.sec]].cur + d );
		break;
	case LKZ_LIST:
		if ( lkm.sec == XP_FORCE && lkm.cur >= 0 ) {
			LK_Raise( lkm.rows[lkm.cur].id, (qboolean)( d > 0 ) );
		} else if ( lkm.sec == XP_CHARACTER ) {
			int character, variant, team;
			UI_PickerGet( &character, &variant, &team );
			UI_PickerSet( character, variant + d );
		}
		break;
	case LKZ_DETAIL:
		LK_FieldChange( lk.df[lkm.sec], d );
		break;
	default:
		LK_Move( d );
		break;
	}
}

static void LK_Press( void ) {
	switch ( lk.zone ) {
	case LKZ_HEAD:
		LK_HeadSet( lk.hf[lkm.sec], lkm.head[lk.hf[lkm.sec]].cur + 1 );
		break;
	case LKZ_DETAIL:
		LK_FieldPress( lk.df[lkm.sec] );
		break;
	case LKZ_COMMIT:
		LK_CommitPress( lk.bf );
		break;
	default:
		if ( lkm.sec == XP_FORCE || lkm.sec == XP_TEAM ) {
			LK_ListPress( lkm.cur );
		} else {
			LK_CommitPress( 0 );	// the primary
		}
		break;
	}
}

static void LK_NextZone( int d ) {
	int z = lk.zone, k;

	for ( k = 0; k < 4; k++ ) {
		z = ( ( z + d ) % 4 + 4 ) % 4;
		if ( LK_ZoneOk( z ) ) {
			lk.zone = z;
			return;
		}
	}
}

// a press on what was drawn at a hit: the painted look's hit list, or the xenui document's events (LK_XenEvent)
static void LK_ClickHit( const lkHit_t *t, int key ) {
	if ( lk.edit != LKE_NONE && t->kind != LKH_NAME ) {
		lk.edit = LKE_NONE;
	}
	switch ( t->kind ) {
	case LKH_TAB:
		lkFx.pressDir = t->idx > lkm.sec ? 1 : -1;
		XP_SetSection( t->idx );
		break;
	case LKH_HEAD:
		lk.zone = LKZ_HEAD;
		lk.hf[lkm.sec] = t->idx;
		LK_HeadSet( t->idx, t->sub );
		break;
	case LKH_LIST:
		lk.zone = LKZ_LIST;
		if ( key == A_MOUSE2 && lkm.sec == XP_FORCE ) {
			lk.focus[XP_FORCE] = t->idx;
			LK_Raise( lkm.rows[t->idx].id, qfalse );
		} else {
			LK_ListPress( t->idx );
		}
		break;
	case LKH_FIELD:
		lk.zone = LKZ_DETAIL;
		lk.df[lkm.sec] = t->idx;
		if ( lkm.fields[t->idx].kind == LKF_BUTTON ) LK_FieldPress( t->idx );
		break;
	case LKH_ARROW:
		lk.zone = LKZ_DETAIL;
		lk.df[lkm.sec] = t->idx;
		LK_FieldChange( t->idx, t->sub ? 1 : -1 );
		break;
	case LKH_CHIP: {
		const lkField_t *f = &lkm.fields[t->idx];
		lk.zone = LKZ_DETAIL;
		lk.df[lkm.sec] = t->idx;
		if ( f->off[0] ) {
			LK_Reject( f->off );
		} else if ( f->kind == LKF_TEAMCHIPS ) {
			if ( UI_PickerTeamAvailable( t->sub ) ) UI_PickerSetTeam( t->sub );
			else LK_Reject( "This outfit has no such colour" );
		} else {
			XP_PressItem( va( "%sicon%s", lkBladeNames[t->sub], f->kind == LKF_BLADE2 ? "2" : "" ) );
		}
		break;
	}
	case LKH_COMMIT:
		lk.zone = LKZ_COMMIT;
		lk.bf = t->idx;
		LK_CommitPress( t->idx );
		break;
	case LKH_NAME:
		lk.edit = lk.edit == LKE_NAME ? LKE_NONE : LKE_NAME;
		lk.editStart = uiInfo.uiDC.realTime;
		break;
	}
}

static void LK_Click( int key ) {
	const float mx = uiInfo.uiDC.cursorx * 3.0f, my = uiInfo.uiDC.cursory * 9.0f / 4.0f;
	const int h = LK_HitAt( mx, my );

	if ( h >= 0 ) {
		LK_ClickHit( &lk.hits[h], key );
	}
}

// the mouse focuses what it is over (heads, fields, buttons; not the list, where focus is the selection)
static void LK_HoverHit( const lkHit_t *t ) {
	switch ( t->kind ) {
	case LKH_HEAD: lk.zone = LKZ_HEAD; lk.hf[lkm.sec] = t->idx; break;
	case LKH_FIELD: case LKH_ARROW: case LKH_CHIP: lk.zone = LKZ_DETAIL; lk.df[lkm.sec] = t->idx; break;
	case LKH_COMMIT: lk.zone = LKZ_COMMIT; lk.bf = t->idx; break;
	case LKH_LIST: if ( lkm.sec == XP_FORCE || lkm.sec == XP_TEAM ) { lk.zone = LKZ_LIST; lk.focus[lkm.sec] = t->idx; } break;
	default: break;
	}
}

static void LK_Hover( void ) {
	const float mx = uiInfo.uiDC.cursorx * 3.0f, my = uiInfo.uiDC.cursory * 9.0f / 4.0f;
	int h;

	if ( mx == lk.mouseX && my == lk.mouseY ) {
		return;
	}
	lk.mouseX = mx;
	lk.mouseY = my;
	if ( ( h = LK_HitAt( mx, my ) ) >= 0 ) {
		LK_HoverHit( &lk.hits[h] );
	}
}

// a text field takes the keys: printable characters, Backspace; Enter, Esc or Tab end it
static qboolean LK_EditKey( int key ) {
	const char *cvar = lk.edit == LKE_NAME ? "ui_Name" : "ui_SaveFCF";
	const int max = lk.edit == LKE_NAME ? MAX_NETNAME - 1 : 12;
	char buf[MAX_NETNAME + 1];
	int len;

	trap->Cvar_VariableStringBuffer( cvar, buf, sizeof( buf ) );
	len = strlen( buf );
	if ( key & K_CHAR_FLAG ) {
		const int ch = key & ~K_CHAR_FLAG;
		if ( ch >= 32 && ch < 127 && len < max && !( lk.edit == LKE_SAVE && ( ch == ' ' || ch == '/' || ch == '\\' || ch == '.' ) ) ) {
			buf[len] = (char)ch;
			buf[len + 1] = 0;
			trap->Cvar_Set( cvar, buf );
		}
		return qtrue;
	}
	switch ( key ) {
	case A_BACKSPACE:
		if ( len > 0 ) {
			buf[len - 1] = 0;
			trap->Cvar_Set( cvar, buf );
		}
		return qtrue;
	case A_ENTER: case A_KP_ENTER:
		if ( lk.edit == LKE_SAVE ) {
			lk.edit = LKE_NONE;
			XP_PressItem( "templatesave" );
			return qtrue;
		}
		lk.edit = LKE_NONE;
		return qtrue;
	case A_ESCAPE: case A_TAB:
		lk.edit = LKE_NONE;
		return qtrue;
	case A_MOUSE1: case A_MOUSE2:
		return qfalse;		// a click elsewhere ends it (LK_Click)
	default:
		return qtrue;
	}
}

qboolean UI_PlayerLookKey( int key, qboolean down ) {
	if ( ( key == A_MOUSE1 || key == A_MOUSE2 ) && lkXen > 0 && XenUI_OverlayMouse( key, down ) ) {
		return qtrue;	// the xenui document has the mouse: its events act (LK_XenEvent)
	}
	if ( !down ) {
		return qtrue;
	}
	if ( lkm.numButtons == 0 ) {	// no frame drawn yet
		LK_Model();
	}
	LK_FixFocus();
	if ( lk.edit != LKE_NONE && LK_EditKey( key ) ) {
		return qtrue;
	}
	if ( key & K_CHAR_FLAG ) {
		return qtrue;	// typed characters mean nothing here
	}
	switch ( key ) {
	case A_LOW_Q: case A_CAP_Q:
		lkFx.pressDir = -1;
		XP_Step( -1 );
		break;
	case A_LOW_E: case A_CAP_E:
		lkFx.pressDir = 1;
		XP_Step( 1 );
		break;
	case A_TAB:
		LK_NextZone( trap->Key_IsDown( A_SHIFT ) ? -1 : 1 );
		break;
	case A_CURSOR_UP: case A_KP_8: case A_LOW_W: case A_CAP_W:
		LK_Move( -1 );
		break;
	case A_CURSOR_DOWN: case A_KP_2: case A_LOW_S: case A_CAP_S:
		LK_Move( 1 );
		break;
	case A_MWHEELUP:
		LK_ListMove( -1 );
		break;
	case A_MWHEELDOWN:
		LK_ListMove( 1 );
		break;
	case A_CURSOR_LEFT: case A_KP_4: case A_LOW_A: case A_CAP_A:
		LK_Change( -1 );
		break;
	case A_CURSOR_RIGHT: case A_KP_6: case A_LOW_D: case A_CAP_D:
		LK_Change( 1 );
		break;
	case A_ENTER: case A_KP_ENTER:
		LK_Press();
		break;
	case A_BACKSPACE:
		if ( lkm.sec == XP_FORCE && lk.zone == LKZ_LIST && lkm.cur >= 0 ) LK_Raise( lkm.rows[lkm.cur].id, qfalse );
		break;
	case A_LOW_F: case A_CAP_F:
		UI_PickerToggleFace();
		break;
	case A_SPACE:
		if ( lkm.sec == XP_SABER && LK_SaberType() == 0 ) {
			const int attack = uiForcePowersRank[FP_SABER_OFFENSE];
			const int n = attack < 1 ? 1 : attack > 3 ? 3 : attack;
			lk.stance = ( LK_Stance() + 1 ) % n;
		}
		break;
	case A_1: case A_2: case A_3:
		if ( lkm.sec == XP_CHARACTER && UI_PickerTeamAvailable( key - A_1 ) ) UI_PickerSetTeam( key - A_1 );
		break;
	case A_MOUSE1: case A_MOUSE2:
		LK_Click( key );
		break;
	case A_ESCAPE:
		LK_Sound( "sound/interface/esc.wav" );
		XP_Back();
		break;
	default:
		return qfalse;	// the console key and the like
	}
	return qtrue;
}

void UI_PlayerLookOpen( void ) {
	int s;

	lk.lastSection = -1;	// the next model sets the zone for the section the screen opens at
	lk.zone = LKZ_LIST;
	lk.bf = 0;
	lk.edit = LKE_NONE;
	lk.hand = 0;
	lk.tpl = 0;
	lk.numHits = 0;
	lk.hover = -1;
	lk.stance = -1;
	lk.gestureFor = -1;
	lk.stageSection = -1;
	for ( s = 0; s < XP_NUM_SECTIONS; s++ ) {
		lk.hf[s] = lk.df[s] = 0;
	}
	lk.focus[XP_FORCE] = 0;
	lk.focus[XP_TEAM] = 0;
	lkFx.sec = -1;		// no ghosts: the screen arrives as a whole
	lkFx.op = lkFx.op == 1 ? 2 : 1;
}

/* ------------------------------------------------------------------ xenui (RmlUi) */

/*
The same screen drawn by an RmlUi document (ui/xen/player.rml, docs/wishlist/ui-system.md phase 1) when xenui is
available and ui_xenPlayer is 1. This file still builds the model, keeps the keys, the zones and the focus, and runs
every action; the document draws the model ("xp", published here each frame) and sends the mouse back as events with
the painted look's hit kinds, so a click does what a click on the painted look does. Without xenui, or when the
document does not load, the painted look above draws as before.
*/

static vmCvar_t	ui_xenPlayer;
static qboolean	lkXenMoved;		// the cursor moved since the last frame: only then does hovering move the focus
static int		lkXenCursor[2] = { -1, -1 };

static float	lkXenStep;		// the list's entering stagger (seconds a row)

static const char *lkStyleNames[] = { "paper", "v", "n", "lt", "dk", "red", "blue", "mute", "costno", "max" };

// the look's strings are Latin-1 (the middle dot is 0xB7): RmlUi reads UTF-8
static void LK_XS( const char *path, const char *s ) {
	char out[512];
	int n = 0;

	for ( ; *s && n < (int)sizeof( out ) - 3; s++ ) {
		const unsigned char c = (unsigned char)*s;
		if ( c < 0x80 ) {
			out[n++] = (char)c;
		} else {
			out[n++] = (char)( 0xC0 | ( c >> 6 ) );
			out[n++] = (char)( 0x80 | ( c & 0x3F ) );
		}
	}
	out[n] = 0;
	XenUI_SetString( lkXen, path, out );
}

static void LK_XTag( const char *list, int k, const char *t, int style ) {
	char p[64];

	Com_sprintf( p, sizeof( p ), "%s[%d].t", list, k );
	LK_XS( p, t );
	Com_sprintf( p, sizeof( p ), "%s[%d].style", list, k );
	XenUI_SetString( lkXen, p, lkStyleNames[style >= 0 && style < (int)ARRAY_LEN( lkStyleNames ) ? style : 0] );
}

static void LK_XHex( const char *path, const float *c ) {
	XenUI_SetString( lkXen, path, va( "#%02x%02x%02x", (int)( c[0] * 255 + 0.5f ), (int)( c[1] * 255 + 0.5f ), (int)( c[2] * 255 + 0.5f ) ) );
}

/*
Motion (player-screen.md 10.8, as the mockup plays it). RmlUi starts an element's animations when its
`animation` value changes, so every event flips between two copies of the same keyframes (a, b; ui/xen/player.rcss):
- a section change flips fx.sp and sets fx.dr from the pressed direction: rows, head, group heads and fields enter
  from that side with a stagger (their anim strings), the old section's rows and fields leave as ghosts for 140 ms, the
  veil crosses the figure and the paint shifts against the direction (the underlay, ui/xen/player_paint.rml);
- an item's fx code (LK_Fx): 1-2 its value slides in from the left, 3-4 from the right, 5-6 it shakes (a refusal),
  7-8 it pops (pips raised, a button pressed);
- fx.cp the confirm bloom, fx.op the screen opening, fx.rp the status line's refusal flash.
*/

// the section the screen shows: on a change, the old rows and fields become ghosts and the paint moves
static void LK_XenMotion( int sec ) {
	if ( sec == lkFx.sec ) {
		return;
	}
	if ( lkFx.sec >= 0 ) {
		lkFx.dir = lkFx.pressDir ? lkFx.pressDir : sec > lkFx.sec ? 1 : -1;
		lkFx.sp = lkFx.sp == 1 ? 2 : 1;
		lkFx.changeAt = uiInfo.uiDC.realTime;
		lkFx.numGhostRows = lkFx.numRows;
		memcpy( lkFx.ghostRows, lkFx.rows, sizeof( lkFx.rows ) );
		lkFx.numGhostFields = lkFx.numFields;
		memcpy( lkFx.ghostFields, lkFx.fields, sizeof( lkFx.fields ) );
		lkFx.wx = Com_Clamp( -240, 240, lkFx.wx - lkFx.dir * 40 );
		lkFx.wy = Com_Clamp( -90, 90, lkFx.wy - lkFx.dir * 14 );
	} else {
		lkFx.numGhostRows = lkFx.numGhostFields = 0;
	}
	lkFx.pressDir = 0;
	lkFx.sec = sec;
	memset( lkFx.head, 0, sizeof( lkFx.head ) );	// codes belong to the old section's items
	memset( lkFx.row, 0, sizeof( lkFx.row ) );
	memset( lkFx.field, 0, sizeof( lkFx.field ) );
	memset( lkFx.button, 0, sizeof( lkFx.button ) );
}

// an entering item's animation: this change's copy and side of the keyframes kind (rin, hin, fin), after n steps
static const char *LK_XenEnter( const char *kind, int n, float step ) {
	return va( "0.22s %.3fs cubic-out %s-%c%c", n * step, kind, lkFx.dir > 0 ? 'r' : 'l', lkFx.sp == 2 ? 'b' : 'a' );
}

// the stagger for count items: step seconds each, all within 80 ms (as the mockup's enter)
static float LK_XenStagger( int count, float step ) {
	return count > 1 && step > 0.08f / ( count - 1 ) ? 0.08f / ( count - 1 ) : step;
}

static void LK_XenRow( int n, int i, float x, float y, const lkListCfg_t *cfg, qboolean active ) {
	const lkRow_t *r = &lkm.rows[i];
	const qboolean cur = (qboolean)( i == lkm.cur ), off = (qboolean)( r->why[0] != 0 );
	float alpha = cur ? 1.0f : active ? 0.92f : 0.8f;
	char p[64];
	int k;

	if ( off ) {
		alpha *= cur ? 0.7f : 0.4f;
	}
#define LK_XP( field )	( Com_sprintf( p, sizeof( p ), "rows[%d]." field, n ), p )
	XenUI_SetInt( lkXen, LK_XP( "i" ), i );
	LK_XS( LK_XP( "word" ), r->word );
	XenUI_SetBool( lkXen, LK_XP( "cur" ), cur );
	XenUI_SetBool( lkXen, LK_XP( "on" ), cur && active );
	XenUI_SetBool( lkXen, LK_XP( "off" ), off );
	XenUI_SetFloat( lkXen, LK_XP( "x" ), x );
	XenUI_SetFloat( lkXen, LK_XP( "y" ), y );
	XenUI_SetFloat( lkXen, LK_XP( "size" ), 64.0f * ( cur ? cfg->sSel : cfg->s ) );
	XenUI_SetFloat( lkXen, LK_XP( "alpha" ), alpha );
	XenUI_SetFloat( lkXen, LK_XP( "gap" ), cfg->gap );
	XenUI_SetInt( lkXen, LK_XP( "pips" ), r->pips );
	LK_XS( LK_XP( "pl" ), r->pl );
	XenUI_SetInt( lkXen, LK_XP( "fx" ), i < LK_MAX_ROWS ? lkFx.row[i] : 0 );
	XenUI_SetString( lkXen, LK_XP( "anim" ), LK_XenEnter( "rin", n, lkXenStep ) );
	if ( n < LK_MAX_ROWS ) {	// for the ghosts of the next section change
		Q_strncpyz( lkFx.rows[n].word, r->word, sizeof( lkFx.rows[n].word ) );
		lkFx.rows[n].x = x;
		lkFx.rows[n].y = y;
		lkFx.rows[n].size = 64.0f * ( cur ? cfg->sSel : cfg->s );
	}
	{	// as LK_RowTags: tags that would cross into the detail column (LK_LIST_RIGHT) are left out
		const float s = cur ? cfg->sSel : cfg->s;
		float tx = x + LK_Width( LK_DISPU, 64.0f * s, 0, r->word, LKT_UPPER ) + 26.0f * s + cfg->gap;
		int shown = 0;
		if ( r->pips >= 0 ) tx += 3 * 22 + 2 * 5 + 8;	// LK_Pips
		Com_sprintf( p, sizeof( p ), "rows[%d].tags", n );
		for ( k = 0; k < r->numTags; k++ ) {
			const float w = LK_Width( LK_TAG, 15, 0.04f, r->tags[k].t, LKT_UPPER ) + 18;
			if ( tx + w > LK_LIST_RIGHT ) break;
			LK_XTag( p, shown++, r->tags[k].t, r->tags[k].style );
			tx += w + 8;
		}
		XenUI_SetCount( lkXen, p, shown );
	}
#undef LK_XP
}

static void LK_XenList( void ) {
	const lkListCfg_t *cfg = &lkLists[lkm.sec];
	const qboolean active = (qboolean)( lk.zone == LKZ_LIST );
	int i, n = 0, g = 0;

	lkXenStep = LK_XenStagger( lkm.sec == XP_FORCE || lkm.numRows < cfg->win ? lkm.numRows : cfg->win, 0.025f );
	if ( lkm.sec == XP_FORCE ) {	// as LK_PaintList: two columns of two groups
		static const int columns[2][2] = { { 0, 1 }, { 2, 3 } };
		static const char *heads[4] = { "NEUTRAL", "LIGHT SIDE", "DARK SIDE", "SABER" };
		static const int headStyles[4] = { LKS_N, LKS_LT, LKS_DK, LKS_V };
		int col, gi;
		for ( col = 0; col < 2; col++ ) {
			const float cx = col ? 1180 : 840;
			float y = cfg->y0;
			for ( gi = 0; gi < 2; gi++ ) {
				const int grp = columns[col][gi];
				qboolean any = qfalse;
				char p[64];
				for ( i = 0; i < lkm.numRows; i++ ) if ( lkm.rows[i].group == grp ) any = qtrue;
				if ( !any ) continue;
				Com_sprintf( p, sizeof( p ), "gheads[%d].x", g );
				XenUI_SetFloat( lkXen, p, cx + ( y - cfg->y0 ) * LK_SLOPE - 2 );
				Com_sprintf( p, sizeof( p ), "gheads[%d].y", g );
				XenUI_SetFloat( lkXen, p, y - 30 );
				Com_sprintf( p, sizeof( p ), "gheads[%d].anim", g );
				XenUI_SetString( lkXen, p, LK_XenEnter( "rin", g, 0.04f ) );
				LK_XTag( "gheads", g++, heads[grp], headStyles[grp] );
				y += 14;
				for ( i = 0; i < lkm.numRows; i++ ) {
					if ( lkm.rows[i].group != grp ) continue;
					y += cfg->step;
					if ( i == lkm.cur ) y += cfg->big;
					LK_XenRow( n++, i, cx + ( y - cfg->y0 ) * LK_SLOPE, y, cfg, active );
				}
				y += 44;
			}
		}
	} else {
		const int count = lkm.numRows, f = lkm.cur < 0 ? 0 : lkm.cur;
		int top = count <= cfg->win ? 0 : f - cfg->lead;
		if ( top > count - cfg->win ) top = count - cfg->win;
		if ( top < 0 ) top = 0;
		for ( i = top; i < count && i < top + cfg->win; i++ ) {
			const int k = i - top;
			const float y = cfg->y0 + k * cfg->step + ( i > f ? cfg->big : 0 ) + ( i == f ? cfg->big : 0 );
			LK_XenRow( n++, i, cfg->x0 + ( y - cfg->y0 ) * LK_SLOPE, y, cfg, active );
		}
	}
	XenUI_SetCount( lkXen, "rows", n );
	XenUI_SetCount( lkXen, "gheads", g );
	lkFx.numRows = n < LK_MAX_ROWS ? n : LK_MAX_ROWS;
}

static void LK_XenHead( void ) {
	const qboolean active = (qboolean)( lk.zone == LKZ_HEAD );
	char p[64];
	int i, k;

	XenUI_SetCount( lkXen, "head", lkm.numHead );
	for ( i = 0; i < lkm.numHead; i++ ) {
		const lkHead_t *h = &lkm.head[i];
#define LK_XP( field )	( Com_sprintf( p, sizeof( p ), "head[%d]." field, i ), p )
		XenUI_SetString( lkXen, LK_XP( "kind" ), h->kind == LKHD_SEG ? "seg" : h->kind == LKHD_TEXT ? "text" : "meter" );
		LK_XS( LK_XP( "k" ), h->k );
		LK_XS( LK_XP( "txt" ), h->txt );
		LK_XS( LK_XP( "off" ), h->off );
		XenUI_SetBool( lkXen, LK_XP( "on" ), active && i == lk.hf[lkm.sec] );
		XenUI_SetInt( lkXen, LK_XP( "fx" ), i < LK_MAX_HEAD ? lkFx.head[i] : 0 );
		XenUI_SetString( lkXen, LK_XP( "anim" ), LK_XenEnter( "hin", i, LK_XenStagger( lkm.numHead, 0.03f ) ) );
		Com_sprintf( p, sizeof( p ), "head[%d].opts", i );
		XenUI_SetCount( lkXen, p, h->numOpts );
		for ( k = 0; k < h->numOpts; k++ ) {
			Com_sprintf( p, sizeof( p ), "head[%d].opts[%d].t", i, k );
			LK_XS( p, h->opts[k] );
			Com_sprintf( p, sizeof( p ), "head[%d].opts[%d].sel", i, k );
			XenUI_SetBool( lkXen, p, k == h->cur );
		}
		// every head carries the meter's values (a hidden element's styles still read them), each set once a frame: a
		// value that flips within the frame dirties the head and lays the document out again
		if ( h->kind != LKHD_METER ) {
			XenUI_SetFloat( lkXen, LK_XP( "used" ), 0 );
			XenUI_SetFloat( lkXen, LK_XP( "prev" ), 0 );
			XenUI_SetBool( lkXen, LK_XP( "prevno" ), qfalse );
		} else {	// as LK_PaintHead: rank, used, the focused row's next cost as a preview
			const char *no = XP_NoForceReason( &lkm.c );
			const int rank = uiForceRank >= 0 && uiForceRank < NUM_FORCE_MASTERY_LEVELS ? uiForceRank : 0;
			const int total = forceMasteryPoints[rank], used = total - uiForceAvailable;
			float uw = 0, pw = 0;
			qboolean pno = qfalse;
			if ( !no && total > 0 ) {
				uw = 300.0f * used / total;
				if ( lk.zone == LKZ_LIST && lkm.cur >= 0 && lkm.cur < lkm.numRows ) {
					const int pp = lkm.rows[lkm.cur].id;
					if ( uiForcePowersRank[pp] < FORCE_LEVEL_3 && !lkm.rows[lkm.cur].why[0] ) {
						const int cost = bgForcePowerCost[pp][uiForcePowersRank[pp] + 1];
						pw = 300.0f * ( cost < total - used ? cost : total - used ) / total;
						pno = (qboolean)( cost > uiForceAvailable );
					}
				}
			}
			LK_XS( LK_XP( "rank" ), XP_RankName( rank ) );
			XenUI_SetFloat( lkXen, LK_XP( "used" ), uw );
			XenUI_SetFloat( lkXen, LK_XP( "prev" ), pw );
			XenUI_SetBool( lkXen, LK_XP( "prevno" ), pno );
			XenUI_SetString( lkXen, LK_XP( "num" ), no ? "-" : va( "%d", uiForceAvailable ) );
			XenUI_SetString( lkXen, LK_XP( "of" ), no ? "locked" : va( "of %d left", total ) );
			XenUI_SetBool( lkXen, LK_XP( "locked" ), no != NULL );
		}
#undef LK_XP
	}
}

static void LK_XenDetail( void ) {
	const qboolean active = (qboolean)( lk.zone == LKZ_DETAIL );
	float y = 0;
	char p[64];
	int i, k;

	XenUI_SetCount( lkXen, "fields", lkm.numFields );
	for ( i = 0; i < lkm.numFields; i++ ) {
		const lkField_t *f = &lkm.fields[i];
		const qboolean stat = (qboolean)( f->kind == LKF_STATIC );
		const qboolean chips = (qboolean)( f->kind == LKF_TEAMCHIPS || f->kind == LKF_BLADE1 || f->kind == LKF_BLADE2 );
#define LK_XP( field )	( Com_sprintf( p, sizeof( p ), "fields[%d]." field, i ), p )
		LK_XS( LK_XP( "k" ), f->k );
		LK_XS( LK_XP( "v" ), f->v );
		LK_XS( LK_XP( "n" ), f->n );
		LK_XS( LK_XP( "off" ), f->off );
		XenUI_SetString( lkXen, LK_XP( "kind" ), stat ? "static" : chips ? "chips" : f->kind == LKF_BUTTON ? "button" : "value" );
		XenUI_SetBool( lkXen, LK_XP( "on" ), active && i == lk.df[lkm.sec] );
		XenUI_SetBool( lkXen, LK_XP( "arrows" ), f->arrows );
		XenUI_SetBool( lkXen, LK_XP( "editing" ), f->act == LKA_SAVE && lk.edit == LKE_SAVE );
		XenUI_SetFloat( lkXen, LK_XP( "x" ), 1440 + y * LK_TAN12 * 0.5f );
		XenUI_SetFloat( lkXen, LK_XP( "y" ), 280 + y );
		XenUI_SetInt( lkXen, LK_XP( "fx" ), i < LK_MAX_FIELDS ? lkFx.field[i] : 0 );
		XenUI_SetString( lkXen, LK_XP( "anim" ), LK_XenEnter( "fin", i, LK_XenStagger( lkm.numFields, 0.03f ) ) );
		if ( i < LK_MAX_FIELDS ) {
			Q_strncpyz( lkFx.fields[i].k, f->k, sizeof( lkFx.fields[i].k ) );
			Q_strncpyz( lkFx.fields[i].v, chips ? "" : f->v, sizeof( lkFx.fields[i].v ) );
			lkFx.fields[i].x = 1440 + y * LK_TAN12 * 0.5f;
			lkFx.fields[i].y = 280 + y;
		}
		Com_sprintf( p, sizeof( p ), "fields[%d].chips", i );
		if ( chips ) {
			const qboolean team = (qboolean)( f->kind == LKF_TEAMCHIPS );
			const int count = team ? 3 : 6;
			int cur, character, variant, t;
			UI_PickerGet( &character, &variant, &t );
			cur = team ? t : LK_BladeIndex( f->kind == LKF_BLADE1 ? "ui_saber_color" : "ui_saber2_color" );
			XenUI_SetCount( lkXen, p, count );
			for ( k = 0; k < count; k++ ) {
				const qboolean usable = (qboolean)( !team || ( !f->off[0] && UI_PickerTeamAvailable( k ) ) );
				Com_sprintf( p, sizeof( p ), "fields[%d].chips[%d].c", i, k );
				LK_XHex( p, team ? lkTeamChips[k] : lkBlades[k] );
				Com_sprintf( p, sizeof( p ), "fields[%d].chips[%d].on", i, k );
				XenUI_SetBool( lkXen, p, k == cur );
				Com_sprintf( p, sizeof( p ), "fields[%d].chips[%d].x", i, k );
				XenUI_SetBool( lkXen, p, !usable && k != cur );
			}
		} else {
			XenUI_SetCount( lkXen, p, 0 );
		}
#undef LK_XP
		y += ( stat ? 60.0f : 76.0f );
		if ( f->off[0] ) {
			y += 22;
		}
	}
	lkFx.numFields = lkm.numFields < LK_MAX_FIELDS ? lkm.numFields : LK_MAX_FIELDS;
}

static void LK_XenTop( void ) {
	static const char *words[XP_NUM_SECTIONS] = { "CHARACTER", "SABER", "FORCE", "TEAM" };
	char name[MAX_NETNAME], info[MAX_INFO_STRING], p[64], summary[128];
	const int rank = uiForceRank >= 0 && uiForceRank < NUM_FORCE_MASTERY_LEVELS ? uiForceRank : 0;
	const qboolean blink = (qboolean)( ( ( uiInfo.uiDC.realTime - lk.editStart ) / 500 ) % 2 == 0 );
	int character, variant, team, i, n = 0;

	trap->Cvar_VariableStringBuffer( "ui_Name", name, sizeof( name ) );
	LK_XS( "name.v", name );
	XenUI_SetBool( lkXen, "name.edit", lk.edit == LKE_NAME );
	XenUI_SetBool( lkXen, "name.caret", lk.edit == LKE_NAME && blink );
	XenUI_SetBool( lkXen, "caret", blink );

	// the strip (as LK_PaintStrip)
	if ( !lkm.c.connected ) {
		LK_XTag( "strip", n++, "MAIN MENU", LKS_MUTE );
	} else {
		trap->GetConfigString( CS_SERVERINFO, info, sizeof( info ) );
		LK_XTag( "strip", n++, XP_GameTypeName( lkm.c.gametype ), LKS_V );
		LK_XTag( "strip", n++, Info_ValueForKey( info, "mapname" ), LKS_MUTE );
	}
	LK_XTag( "strip", n++, va( "%s " XP_DOT " %d PTS", XP_RankName( rank ), forceMasteryPoints[rank] ), LKS_MUTE );
	if ( lkm.c.connected ) {
		int off = 0;
		if ( lkm.c.forceBasedTeams ) LK_XTag( "strip", n++, "FORCE TEAMS", LKS_DK );
		for ( i = 0; i < NUM_FORCE_POWERS; i++ ) if ( lkm.c.disabledForce & ( 1 << i ) ) off++;
		if ( off && off < NUM_FORCE_POWERS ) LK_XTag( "strip", n++, va( "%d POWER%s OFF", off, off == 1 ? "" : "S" ), LKS_DK );
	}
	XenUI_SetCount( lkXen, "strip", n );

	// the character's name and its tags (as LK_PaintCharName)
	UI_PickerGet( &character, &variant, &team );
	XenUI_SetBool( lkXen, "char.show", character >= 0 );
	n = 0;
	if ( character >= 0 ) {
		const int state = UI_PickerStageState();
		LK_XS( "char.name", uiRoster.characters[character].name );
		LK_XTag( "char.tags", n++, uiRoster.variants[uiRoster.characters[character].firstVariant + variant].name, LKS_V );
		LK_XTag( "char.tags", n++, LK_StageTag( state ),
			state >= 3 ? LKS_DK : LKS_MUTE );
	}
	XenUI_SetCount( lkXen, "char.tags", n );

	for ( i = 0; i < XP_NUM_SECTIONS; i++ ) {
		XP_Summary( i, summary, sizeof( summary ) );
		Com_sprintf( p, sizeof( p ), "tabs[%d].word", i );
		XenUI_SetString( lkXen, p, words[i] );
		Com_sprintf( p, sizeof( p ), "tabs[%d].summary", i );
		LK_XS( p, summary );
		Com_sprintf( p, sizeof( p ), "tabs[%d].cur", i );
		XenUI_SetBool( lkXen, p, i == lkm.sec );
		Com_sprintf( p, sizeof( p ), "tabs[%d].hidden", i );
		XenUI_SetBool( lkXen, p, i == XP_TEAM && !lkm.c.connected );
		Com_sprintf( p, sizeof( p ), "tabs[%d].x", i );
		XenUI_SetFloat( lkXen, p, 820 + i * 258.0f );
		Com_sprintf( p, sizeof( p ), "tabs[%d].pulse", i );	// a spectator's way in: Team calls until opened
		XenUI_SetBool( lkXen, p, i == XP_TEAM && lkm.c.connected && lkm.c.team == TEAM_SPECTATOR && lkm.sec != XP_TEAM );
	}
}

static void LK_XenBottom( void ) {
	qboolean warn;
	const char *s = XP_StatusLine( &warn );
	char p[64];
	int i;

	LK_XS( "status.t", s );
	XenUI_SetBool( lkXen, "status.warn", warn );
	XenUI_SetCount( lkXen, "buttons", lkm.numButtons );
	for ( i = 0; i < lkm.numButtons; i++ ) {
		const lkButton_t *b = &lkm.buttons[i];
		Com_sprintf( p, sizeof( p ), "buttons[%d].t", i );
		LK_XS( p, b->t );
		Com_sprintf( p, sizeof( p ), "buttons[%d].primary", i );
		XenUI_SetBool( lkXen, p, b->primary );
		Com_sprintf( p, sizeof( p ), "buttons[%d].on", i );
		XenUI_SetBool( lkXen, p, lk.zone == LKZ_COMMIT && i == lk.bf );
		Com_sprintf( p, sizeof( p ), "buttons[%d].swatch", i );
		if ( b->swatch ) LK_XHex( p, b->swatch );
		else XenUI_SetString( lkXen, p, "#00000000" );	// a valid colour: a hidden element's styles still read it
		Com_sprintf( p, sizeof( p ), "buttons[%d].sw", i );
		XenUI_SetBool( lkXen, p, b->swatch != NULL );
		Com_sprintf( p, sizeof( p ), "buttons[%d].fx", i );
		XenUI_SetInt( lkXen, p, i < LK_MAX_BUTTONS ? lkFx.button[i] : 0 );
	}
	LK_XS( "hints.primary", lkm.numButtons ? lkm.buttons[0].t : "" );
}

// the document's mouse: hit( kind, index, sub ) on a press (either button, as the painted look acts at the press: a
// right press on a Force row lowers it), over on entering
static void LK_XenEvent( const char *event, int argc, const char **argv, int button ) {
	static const char *kinds[] = { "tab", "head", "row", "field", "arrow", "chip", "commit", "name" };
	lkHit_t t;
	int k;

	if ( argc < 1 ) {
		return;
	}
	memset( &t, 0, sizeof( t ) );
	t.kind = -1;
	for ( k = 0; k < (int)ARRAY_LEN( kinds ); k++ ) {
		if ( !Q_stricmp( argv[0], kinds[k] ) ) t.kind = k;	// the order of LKH_*
	}
	if ( t.kind < 0 ) {
		return;
	}
	t.idx = argc > 1 ? atoi( argv[1] ) : 0;
	t.sub = argc > 2 ? atoi( argv[2] ) : 0;
	// the document's indices come from its own frame: drop any the model in hand does not have
	switch ( t.kind ) {
	case LKH_TAB: if ( t.idx < 0 || t.idx >= XP_NUM_SECTIONS ) return; break;
	case LKH_HEAD: if ( t.idx < 0 || t.idx >= lkm.numHead ) return; break;
	case LKH_LIST: if ( t.idx < 0 || t.idx >= lkm.numRows ) return; break;
	case LKH_FIELD: case LKH_ARROW: if ( t.idx < 0 || t.idx >= lkm.numFields ) return; break;
	case LKH_CHIP:
		if ( t.idx < 0 || t.idx >= lkm.numFields || t.sub < 0 || t.sub >= (int)ARRAY_LEN( lkBladeNames ) ) return;
		break;
	case LKH_COMMIT: if ( t.idx < 0 || t.idx >= lkm.numButtons ) return; break;
	default: break;
	}
	if ( !Q_stricmp( event, "hit" ) ) {
		LK_ClickHit( &t, button == 1 ? A_MOUSE2 : A_MOUSE1 );
	} else if ( !Q_stricmp( event, "over" ) ) {
		// RmlUi also reports an element moving under a still pointer (rows and fields move with every key): as the
		// painted look (LK_Hover), only a moving mouse focuses what it is over
		if ( lkXenMoved ) LK_HoverHit( &t );
	}
}

// the old section's rows and fields while they leave (140 ms), and the motion's parities
static void LK_XenFx( void ) {
	const qboolean ghosts = (qboolean)( uiInfo.uiDC.realTime - lkFx.changeAt < 160 );
	char p[64];
	int i;

	XenUI_SetInt( lkXen, "fx.sp", lkFx.sp );
	XenUI_SetBool( lkXen, "fx.dr", lkFx.dir > 0 );
	XenUI_SetInt( lkXen, "fx.cp", lkFx.cp );
	XenUI_SetInt( lkXen, "fx.op", lkFx.op );
	XenUI_SetInt( lkXen, "fx.rp", lkFx.rp );
	for ( i = 0; ghosts && i < lkFx.numGhostRows; i++ ) {
		Com_sprintf( p, sizeof( p ), "ghosts[%d].word", i );
		LK_XS( p, lkFx.ghostRows[i].word );
		Com_sprintf( p, sizeof( p ), "ghosts[%d].x", i );
		XenUI_SetFloat( lkXen, p, lkFx.ghostRows[i].x );
		Com_sprintf( p, sizeof( p ), "ghosts[%d].y", i );
		XenUI_SetFloat( lkXen, p, lkFx.ghostRows[i].y );
		Com_sprintf( p, sizeof( p ), "ghosts[%d].size", i );
		XenUI_SetFloat( lkXen, p, lkFx.ghostRows[i].size );
	}
	XenUI_SetCount( lkXen, "ghosts", ghosts ? lkFx.numGhostRows : 0 );
	for ( i = 0; ghosts && i < lkFx.numGhostFields; i++ ) {
		Com_sprintf( p, sizeof( p ), "gfields[%d].k", i );
		LK_XS( p, lkFx.ghostFields[i].k );
		Com_sprintf( p, sizeof( p ), "gfields[%d].v", i );
		LK_XS( p, lkFx.ghostFields[i].v );
		Com_sprintf( p, sizeof( p ), "gfields[%d].x", i );
		XenUI_SetFloat( lkXen, p, lkFx.ghostFields[i].x );
		Com_sprintf( p, sizeof( p ), "gfields[%d].y", i );
		XenUI_SetFloat( lkXen, p, lkFx.ghostFields[i].y );
	}
	XenUI_SetCount( lkXen, "gfields", ghosts ? lkFx.numGhostFields : 0 );
}

// the screen's model, once ui_xenPlayer allows it
static void LK_XenInit( void ) {
	if ( lkXen < 0 ) {
		trap->Cvar_Register( &ui_xenPlayer, "ui_xenPlayer", "1", CVAR_ARCHIVE );	// xenjk: the player screen's look is drawn from its RML document (xenui); 0 = the painted look
		lkXen = ui_xenPlayer.integer ? XenUI_ModelCreate( "xp", LK_XenEvent ) : 0;
		if ( lkXen > 0 ) {
			XenUI_ModelEvent( lkXen, "hit" );
			XenUI_ModelEvent( lkXen, "over" );
		}
	}
	trap->Cvar_Update( &ui_xenPlayer );
}

static int		lkXenUnder = -1;	// the underlay's model ("xpu"), 0 when unavailable
static qboolean	lkXenUnderOn;		// the underlay drew this frame (the document then draws the ink wash and vignette)

// the screen's layer 0 (under the stage) with ui/xen/player_paint.rml: the painted world, its drift and parallax, the
// section's bloom, the team wash, the grain; qfalse: the backdrop picture draws it
static qboolean LK_XenUnder( int sec ) {
	static const float red[3] = { 0.898f, 0.282f, 0.302f }, blue[3] = { 0.243f, 0.545f, 1.0f }, other[3] = { 0.506f, 0.659f, 1.0f };
	const float *team;

	lkXenUnderOn = qfalse;
	LK_XenInit();
	if ( lkXen <= 0 || !ui_xenPlayer.integer ) {
		return qfalse;
	}
	if ( lkXenUnder < 0 ) {
		lkXenUnder = XenUI_UnderlayModelCreate( "xpu" );
	}
	if ( lkXenUnder <= 0 ) {
		return qfalse;
	}
	LK_XenMotion( sec );
	team = lkm.c.team == TEAM_RED ? red : lkm.c.team == TEAM_BLUE ? blue : other;
	XenUI_SetInt( lkXenUnder, "sec", sec );
	XenUI_SetFloat( lkXenUnder, "wx", lkFx.wx );
	XenUI_SetFloat( lkXenUnder, "wy", lkFx.wy );
	XenUI_SetInt( lkXenUnder, "op", lkFx.op );
	XenUI_SetString( lkXenUnder, "team", va( "#%02x%02x%02x", (int)( team[0] * 255 + 0.5f ), (int)( team[1] * 255 + 0.5f ),
		(int)( team[2] * 255 + 0.5f ) ) );
	lkXenUnderOn = (qboolean)XenUI_Underlay( "ui/xen/player_paint.rml" );
	return lkXenUnderOn;
}

// draws the screen's layer 1 with the document; qfalse: the painted look draws it
static qboolean LK_XenDraw( void ) {
	LK_XenInit();
	if ( lkXen <= 0 || !ui_xenPlayer.integer ) {
		return qfalse;
	}
	LK_XenMotion( lkm.sec );
	lkXenMoved = (qboolean)( uiInfo.uiDC.cursorx != lkXenCursor[0] || uiInfo.uiDC.cursory != lkXenCursor[1] );
	lkXenCursor[0] = uiInfo.uiDC.cursorx;
	lkXenCursor[1] = uiInfo.uiDC.cursory;
	XenUI_SetInt( lkXen, "sec", lkm.sec );
	XenUI_SetInt( lkXen, "zone", lk.zone );
	XenUI_SetBool( lkXen, "connected", lkm.c.connected );
	XenUI_SetBool( lkXen, "under", lkXenUnderOn );	// else the backdrop picture has the wash and vignette
	LK_XenFx();
	LK_XenTop();
	LK_XenHead();
	LK_XenList();
	LK_XenDetail();
	LK_XenBottom();
	return (qboolean)XenUI_Overlay( "ui/xen/player.rml" );
}
