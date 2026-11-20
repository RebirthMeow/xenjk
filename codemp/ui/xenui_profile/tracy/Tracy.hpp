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

// xenui's stand-in for Tracy (CMake option XenUIProfile): RmlUi's profiling zones (RMLUI_ZoneScoped and the others)
// become scoped timers that add each zone's inclusive and self time to a per-site total. ui_xenProfile prints them.
// Element::Update, Element::Render and the block layout zones also build the element's address under
// RMLUI_TRACY_PROFILING: that cost lands in their self time, so read those three as inflated.
#pragma once

namespace xenprof {

struct Site {
	const char	*name;
	const char	*file;
	int			line;
	double		inclusive = 0, self = 0;	// milliseconds
	long long	calls = 0;
	Site		*next = nullptr;
	bool		listed = false;
	Site( const char *n, const char *f, int l ) : name( n ), file( f ), line( l ) {}
};

struct Scope {
	Site		*site;
	Scope		*parent;
	long long	start;
	double		children = 0;
	explicit Scope( Site *s );
	~Scope();
};

}

#define XENPROF_CAT2( a, b ) a##b
#define XENPROF_CAT( a, b ) XENPROF_CAT2( a, b )
#define XENPROF_ZONE( name ) \
	static xenprof::Site XENPROF_CAT( xenprofSite, __LINE__ )( name, __FILE__, __LINE__ ); \
	xenprof::Scope XENPROF_CAT( xenprofScope, __LINE__ )( &XENPROF_CAT( xenprofSite, __LINE__ ) )

#define ZoneScoped							XENPROF_ZONE( __FUNCTION__ )
#define ZoneScopedN( name )					XENPROF_ZONE( name )
#define ZoneScopedC( color )				XENPROF_ZONE( __FUNCTION__ )
#define ZoneScopedNC( name, color )			XENPROF_ZONE( name )
#define ZoneNamed( varname, active )		XENPROF_ZONE( __FUNCTION__ )
#define ZoneNamedN( varname, name, active )	XENPROF_ZONE( name )
#define ZoneNamedC( varname, color, active )	XENPROF_ZONE( __FUNCTION__ )
#define ZoneNamedNC( varname, name, color, active )	XENPROF_ZONE( name )
#define ZoneText( txt, size )
#define ZoneName( txt, size )
#define TracyPlot( name, val )
#define FrameMark
#define FrameMarkNamed( name )
#define FrameMarkStart( name )
#define FrameMarkEnd( name )
