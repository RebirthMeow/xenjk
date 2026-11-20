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

// tr_g2joints.cpp -- xenjk: Ghoul2 models bent about their own joints (r_g2Joints); see tr_g2joints.h

#include "tr_g2joints.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G2J_MAX_BONES	256

static int G2J_Token( const char **p, char *out, int outSize )
{
	const char *s = *p;
	for ( ;; )
	{
		while ( *s && isspace( (unsigned char)*s ) )
		{
			s++;
		}
		if ( s[0] == '/' && s[1] == '/' )
		{
			while ( *s && *s != '\n' )
			{
				s++;
			}
			continue;
		}
		break;
	}
	int n = 0;
	while ( *s && !isspace( (unsigned char)*s ) )
	{
		if ( n < outSize - 1 )
		{
			out[n++] = *s;
		}
		s++;
	}
	out[n] = 0;
	*p = s;
	return n;
}

// the next token on the current line (0 at its end or at a // comment; *p is left on the line end)
static int G2J_LineToken( const char **p, char *out, int outSize )
{
	const char *s = *p;
	while ( *s && *s != '\n' && isspace( (unsigned char)*s ) )
	{
		s++;
	}
	if ( !*s || *s == '\n' || ( s[0] == '/' && s[1] == '/' ) )
	{
		*p = s;
		return 0;
	}
	return G2J_Token( p, out, outSize );
}

// past the end of the current line
static void G2J_SkipLine( const char **p )
{
	const char *s = *p;
	while ( *s && *s != '\n' )
	{
		s++;
	}
	*p = s;
}

static int G2J_Find( const char *name, int numBones, const char *const *names )
{
	for ( int i = 0; i < numBones; i++ )
	{
		if ( !strcmp( names[i], name ) )
		{
			return i;
		}
	}
	for ( int i = 0; i < numBones; i++ )
	{
		const char *a = names[i], *b = name;
		while ( *a && *b && tolower( (unsigned char)*a ) == tolower( (unsigned char)*b ) )
		{
			a++;
			b++;
		}
		if ( !*a && !*b )
		{
			return i;
		}
	}
	return -1;
}

static void G2J_Warn( char *warn, int warnSize, const char *text )
{
	if ( warn && warnSize > 0 && !warn[0] )
	{
		snprintf( warn, warnSize, "%s", text );
	}
}

// does following GLA parents and anchors from 'from' reach 'target'?
static bool G2J_Reaches( int from, int target, int numBones, const int *parents, const int *anchor )
{
	int stack[2 * G2J_MAX_BONES + 2], top = 0;
	bool seen[G2J_MAX_BONES] = { false };
	if ( from >= 0 )
	{
		stack[top++] = from;
	}
	while ( top )
	{
		const int b = stack[--top];
		if ( b == target )
		{
			return true;
		}
		if ( seen[b] )
		{
			continue;
		}
		seen[b] = true;
		if ( parents[b] >= 0 && top < 2 * G2J_MAX_BONES )
		{
			stack[top++] = parents[b];
		}
		if ( anchor[b] >= 0 && top < 2 * G2J_MAX_BONES )
		{
			stack[top++] = anchor[b];
		}
	}
	return false;
}

int G2J_Parse( const char *text, int numBones, const char *const *names, const int *parents, const float (*pivots)[3],
	float (*delta)[3], int *anchor, char *warn, int warnSize )
{
	char tok[5][128];
	const char *p = text;

	if ( warn && warnSize > 0 )
	{
		warn[0] = 0;
	}
	if ( !G2J_Token( &p, tok[0], sizeof( tok[0] ) ) || strcmp( tok[0], "xenjk_joints" )
		|| !G2J_Token( &p, tok[1], sizeof( tok[1] ) ) || atoi( tok[1] ) != G2J_VERSION )
	{
		G2J_Warn( warn, warnSize, "not an xenjk_joints 1 file" );
		return -1;
	}
	if ( numBones <= 0 || numBones > G2J_MAX_BONES )
	{
		G2J_Warn( warn, warnSize, "skeleton has too many bones" );
		return 0;
	}

	bool listed[G2J_MAX_BONES] = { false };
	float own[G2J_MAX_BONES][3];
	int fileParent[G2J_MAX_BONES];
	int numListed = 0;
	for ( int i = 0; i < numBones; i++ )
	{
		fileParent[i] = -1;
	}

	// one bone per line: name, the bone of the model's own skeleton that carries it ("-" for none), x y z
	for ( ;; )
	{
		if ( !G2J_Token( &p, tok[0], sizeof( tok[0] ) ) )
		{
			break;
		}
		int got = 1;
		while ( got < 5 && G2J_LineToken( &p, tok[got], sizeof( tok[got] ) ) )
		{
			got++;
		}
		G2J_SkipLine( &p );	// a short line does not take the next line's fields, extra fields are ignored
		if ( got < 5 )
		{
			G2J_Warn( warn, warnSize, "truncated bone line" );
			continue;
		}
		const int b = G2J_Find( tok[0], numBones, names );
		if ( b < 0 )
		{
			continue;	// a bone this skeleton does not have (tags the exporter adds, a different GLA)
		}
		char *end;
		bool ok = true;
		float xyz[3];
		for ( int k = 0; k < 3; k++ )
		{
			xyz[k] = (float)strtod( tok[2 + k], &end );
			ok = ok && end != tok[2 + k] && !*end;
		}
		if ( !ok )
		{
			G2J_Warn( warn, warnSize, "bad coordinate" );
			continue;	// a bone listed before keeps that line's values
		}
		own[b][0] = xyz[0];
		own[b][1] = xyz[1];
		own[b][2] = xyz[2];
		if ( !listed[b] )
		{
			numListed++;
		}
		listed[b] = true;
		fileParent[b] = strcmp( tok[1], "-" ) ? G2J_Find( tok[1], numBones, names ) : -1;
	}

	// offsets: listed bones their own, the rest their nearest listed GLA ancestor's (zero when none)
	for ( int i = 0; i < numBones; i++ )
	{
		int b = i, steps = 0;
		while ( b >= 0 && !listed[b] && steps++ <= numBones )
		{
			b = parents[b];
		}
		for ( int k = 0; k < 3; k++ )
		{
			delta[i][k] = ( b >= 0 && listed[b] ) ? own[b][k] - pivots[b][k] : 0.0f;
		}
	}

	// anchors: the model's own parent where the file names one this skeleton has, else the GLA parent; an anchor
	// that would make a bone depend on itself (through GLA parents and anchors) falls back to the GLA parent
	for ( int i = 0; i < numBones; i++ )
	{
		const int fp = fileParent[i];
		anchor[i] = ( listed[i] && fp >= 0 && fp != i ) ? fp : parents[i];
	}
	for ( bool changed = true; changed; )
	{
		changed = false;
		for ( int i = 0; i < numBones; i++ )
		{
			if ( anchor[i] != parents[i] && G2J_Reaches( anchor[i], i, numBones, parents, anchor ) )
			{
				anchor[i] = parents[i];
				changed = true;
				G2J_Warn( warn, warnSize, "an anchor formed a cycle and fell back to the GLA parent" );
			}
		}
	}
	return numListed;
}

void G2J_Correct( float child[3][4], const float anchor[3][4], const float parentShift[3], const float anchorShift[3],
	const float delta[3], float shiftOut[3] )
{
	for ( int r = 0; r < 3; r++ )
	{
		const float corr = anchorShift[r] - parentShift[r]
			+ anchor[r][0] * delta[0] + anchor[r][1] * delta[1] + anchor[r][2] * delta[2]
			- ( child[r][0] * delta[0] + child[r][1] * delta[1] + child[r][2] * delta[2] );
		child[r][3] += corr;
		shiftOut[r] = parentShift[r] + corr;
	}
}
