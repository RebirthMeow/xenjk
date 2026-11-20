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
xenui: RmlUi in the UI module, beside the stock menus (docs/wishlist/ui-system.md section 4).

The render interface forwards to the renderer's xenui interface (rd-common/xenui_render.h), found in the loaded renderer
module because the installed engine's UI import table has no slot for it. Files come through the engine filesystem;
fonts are every .ttf/.otf in fonts/xenui. One document is open at a time (ui_xenOpen); while it is open it takes the
keys and draws over the stock menus.

Agent and test hooks (section 4a): ui_xenDump writes the live element tree with boxes, states and a few computed
properties as JSON, ui_xenShot writes that dump beside a screenshot, ui_xenSend clicks or focuses an element by
selector, ui_xenKey sends a navigation key, ui_xenStats prints the CPU cost (publish, update, render) and which model
names were dirtied, ui_xenProfile prints RmlUi's own zones in a XenUIProfile build.
*/
#include "xenui.h"
#include "keycodes.h"
#include "rd-common/xenui_render.h"

#include <RmlUi/Core.h>
#include <RmlUi/Debugger.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifdef RMLUI_TRACY_PROFILING
// XenUIProfile: RmlUi's zones as scoped timers (xenui_profile/tracy/Tracy.hpp); single-threaded, as RmlUi is here
namespace xenprof {

Site		*sites;
Scope		*current;
long long	profileFrames;

static long long Ticks() { return std::chrono::steady_clock::now().time_since_epoch().count(); }
static double TicksMs( long long t ) { return t * 1000.0 * std::chrono::steady_clock::period::num / std::chrono::steady_clock::period::den; }

Scope::Scope( Site *s ) : site( s ), parent( current ), start( Ticks() )
{
	if ( !s->listed ) {
		s->listed = true;
		s->next = sites;
		sites = s;
	}
	current = this;
}

Scope::~Scope()
{
	const double ms = TicksMs( Ticks() - start );
	site->inclusive += ms;
	site->self += ms - children;
	site->calls++;
	if ( parent )
		parent->children += ms;
	current = parent;
}

}
using xenprof::profileFrames;
#endif

namespace {

const xenUIHost_t		*host;
const xenUIRenderAPI_t	*re;

void Printf( const char *fmt, ... )
{
	char	text[4096];
	va_list	ap;

	va_start( ap, fmt );
	vsnprintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	host->Print( text );
}

class XenSystem : public Rml::SystemInterface {
public:
	double GetElapsedTime() override { return host->Milliseconds() * 0.001; }

	bool LogMessage( Rml::Log::Type type, const Rml::String &message ) override {
		const char *colour = type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT ? "^1"
			: type == Rml::Log::LT_WARNING ? "^3" : "";
		if ( type <= Rml::Log::LT_INFO )
			Printf( "%sxenui: %s\n", colour, message.c_str() );
		return true;
	}
};

struct XenFile {
	void	*data;
	size_t	length, pos;
};

class XenFiles : public Rml::FileInterface {
public:
	Rml::FileHandle Open( const Rml::String &path ) override {
		void		*buffer;
		const char	*p = path.c_str();

		while ( *p == '/' || *p == '\\' )
			p++;
		const int length = host->FS_ReadFile( p, &buffer );
		if ( length < 0 || !buffer )
			return 0;
		XenFile *f = new XenFile{ buffer, (size_t)length, 0 };
		return (Rml::FileHandle)f;
	}

	void Close( Rml::FileHandle file ) override {
		XenFile *f = (XenFile *)file;
		host->FS_FreeFile( f->data );
		delete f;
	}

	size_t Read( void *buffer, size_t size, Rml::FileHandle file ) override {
		XenFile *f = (XenFile *)file;
		const size_t n = size < f->length - f->pos ? size : f->length - f->pos;
		memcpy( buffer, (const char *)f->data + f->pos, n );
		f->pos += n;
		return n;
	}

	bool Seek( Rml::FileHandle file, long offset, int origin ) override {
		XenFile *f = (XenFile *)file;
		const long base = origin == SEEK_CUR ? (long)f->pos : origin == SEEK_END ? (long)f->length : 0;
		if ( base + offset < 0 || base + offset > (long)f->length )
			return false;
		f->pos = (size_t)( base + offset );
		return true;
	}

	size_t Tell( Rml::FileHandle file ) override { return ( (XenFile *)file )->pos; }
	size_t Length( Rml::FileHandle file ) override { return ( (XenFile *)file )->length; }
};

class XenRender : public Rml::RenderInterface {
public:
	Rml::CompiledGeometryHandle CompileGeometry( Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices ) override {
		static_assert( sizeof( Rml::Vertex ) == sizeof( xenUIVertex_t ), "Rml::Vertex is xenUIVertex_t" );
		return (Rml::CompiledGeometryHandle)re->CompileGeometry( (const xenUIVertex_t *)vertices.data(), (int)vertices.size(),
			indices.data(), (int)indices.size() );
	}
	void RenderGeometry( Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture ) override {
		re->RenderGeometry( (int)geometry, translation.x, translation.y, (int)texture );
	}
	void ReleaseGeometry( Rml::CompiledGeometryHandle geometry ) override { re->ReleaseGeometry( (int)geometry ); }

	Rml::TextureHandle LoadTexture( Rml::Vector2i &dimensions, const Rml::String &source ) override {
		const char *p = source.c_str();
		while ( *p == '/' || *p == '\\' )
			p++;
		return (Rml::TextureHandle)re->LoadTexture( p, &dimensions.x, &dimensions.y );
	}
	Rml::TextureHandle GenerateTexture( Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions ) override {
		return (Rml::TextureHandle)re->CreateTexture( source.data(), dimensions.x, dimensions.y );
	}
	void ReleaseTexture( Rml::TextureHandle texture ) override { re->ReleaseTexture( (int)texture ); }

	void EnableScissorRegion( bool enable ) override {
		scissorOn = enable;
		re->SetScissor( scissorOn, scissor.Left(), scissor.Top(), scissor.Width(), scissor.Height() );
	}
	void SetScissorRegion( Rml::Rectanglei region ) override {
		scissor = region;
		re->SetScissor( scissorOn, scissor.Left(), scissor.Top(), scissor.Width(), scissor.Height() );
	}
	void SetTransform( const Rml::Matrix4f *transform ) override { re->SetTransform( transform ? transform->data() : nullptr ); }

	// version 2: clip masks, layers, filters and gradients (a version 1 renderer keeps RmlUi's defaults: none of them)
	void EnableClipMask( bool enable ) override {
		if ( advanced )
			re->EnableClipMask( enable );
	}
	void RenderToClipMask( Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation ) override {
		if ( advanced )
			re->RenderToClipMask( operation == Rml::ClipMaskOperation::Set ? XENUI_CLIP_SET
				: operation == Rml::ClipMaskOperation::SetInverse ? XENUI_CLIP_SET_INVERSE : XENUI_CLIP_INTERSECT,
				(int)geometry, translation.x, translation.y );
	}
	Rml::LayerHandle PushLayer() override { return advanced ? (Rml::LayerHandle)re->PushLayer() : 0; }
	void CompositeLayers( Rml::LayerHandle source, Rml::LayerHandle destination, Rml::BlendMode blend_mode,
		Rml::Span<const Rml::CompiledFilterHandle> filters ) override {
		int handles[16], n = 0;
		if ( !advanced )
			return;
		for ( Rml::CompiledFilterHandle f : filters ) {
			if ( n < 16 )
				handles[n++] = (int)f;
		}
		re->CompositeLayers( (int)source, (int)destination, blend_mode == Rml::BlendMode::Replace ? XENUI_BLEND_REPLACE : XENUI_BLEND,
			handles, n );
	}
	void PopLayer() override {
		if ( advanced )
			re->PopLayer();
	}
	Rml::TextureHandle SaveLayerAsTexture() override { return advanced ? (Rml::TextureHandle)re->SaveLayerAsTexture() : 0; }
	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override { return advanced ? (Rml::CompiledFilterHandle)re->SaveLayerAsMaskImage() : 0; }
	Rml::CompiledFilterHandle CompileFilter( const Rml::String &name, const Rml::Dictionary &parameters ) override;
	void ReleaseFilter( Rml::CompiledFilterHandle filter ) override {
		if ( advanced )
			re->ReleaseFilter( (int)filter );
	}
	Rml::CompiledShaderHandle CompileShader( const Rml::String &name, const Rml::Dictionary &parameters ) override;
	void RenderShader( Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
		Rml::TextureHandle texture ) override {
		if ( advanced )
			re->RenderShader( (int)shader, (int)geometry, translation.x, translation.y, (int)texture );
	}
	void ReleaseShader( Rml::CompiledShaderHandle shader ) override {
		if ( advanced )
			re->ReleaseShader( (int)shader );
	}

	bool				scissorOn = false;
	Rml::Rectanglei		scissor;
	bool				advanced = false;	// the renderer has version 2
};

template <typename T> T Param( const Rml::Dictionary &parameters, const char *name, T fallback )
{
	auto it = parameters.find( name );
	return it != parameters.end() ? it->second.Get<T>( fallback ) : fallback;
}

// RmlUi's filters as the renderer's: its basic ones as colour matrices on the premultiplied colour (the GL3
// backend's matrices)
Rml::CompiledFilterHandle XenRender::CompileFilter( const Rml::String &name, const Rml::Dictionary &parameters )
{
	xenUIFilter_t	f;
	Rml::Matrix4f	m = Rml::Matrix4f::Identity();

	if ( !advanced )
		return 0;
	memset( &f, 0, sizeof( f ) );
	const float value = Param( parameters, "value", 1.0f );
	f.type = XENUI_FILTER_COLOR_MATRIX;
	if ( name == "opacity" ) {
		f.type = XENUI_FILTER_OPACITY;
		f.value = value;
	} else if ( name == "blur" ) {
		f.type = XENUI_FILTER_BLUR;
		f.sigma = Param( parameters, "sigma", 1.0f );
	} else if ( name == "drop-shadow" ) {
		const Rml::ColourbPremultiplied c = Param( parameters, "color", Rml::Colourb() ).ToPremultiplied();
		const Rml::Vector2f offset = Param( parameters, "offset", Rml::Vector2f( 0, 0 ) );
		f.type = XENUI_FILTER_DROP_SHADOW;
		f.sigma = Param( parameters, "sigma", 0.0f );
		f.offset[0] = offset.x;
		f.offset[1] = offset.y;
		f.color[0] = c.red / 255.0f;
		f.color[1] = c.green / 255.0f;
		f.color[2] = c.blue / 255.0f;
		f.color[3] = c.alpha / 255.0f;
	} else if ( name == "brightness" ) {
		m = Rml::Matrix4f::FromRows( { value, 0, 0, 0 }, { 0, value, 0, 0 }, { 0, 0, value, 0 }, { 0, 0, 0, 1 } );
	} else if ( name == "contrast" ) {
		const float gray = 0.5f - 0.5f * value;
		m = Rml::Matrix4f::FromRows( { value, 0, 0, gray }, { 0, value, 0, gray }, { 0, 0, value, gray }, { 0, 0, 0, 1 } );
	} else if ( name == "invert" ) {
		const float v = Rml::Math::Clamp( value, 0.0f, 1.0f ), d = 1.0f - 2.0f * v;
		m = Rml::Matrix4f::FromRows( { d, 0, 0, v }, { 0, d, 0, v }, { 0, 0, d, v }, { 0, 0, 0, 1 } );
	} else if ( name == "grayscale" ) {
		const float v = Rml::Math::Clamp( value, 0.0f, 1.0f ), r = 1.0f - v;
		const Rml::Vector3f g = v * Rml::Vector3f( 0.2126f, 0.7152f, 0.0722f );
		m = Rml::Matrix4f::FromRows( { g.x + r, g.y, g.z, 0 }, { g.x, g.y + r, g.z, 0 }, { g.x, g.y, g.z + r, 0 }, { 0, 0, 0, 1 } );
	} else if ( name == "sepia" ) {
		const float v = Rml::Math::Clamp( value, 0.0f, 1.0f ), r = 1.0f - v;
		const Rml::Vector3f rm = v * Rml::Vector3f( 0.393f, 0.769f, 0.189f ), gm = v * Rml::Vector3f( 0.349f, 0.686f, 0.168f ),
			bm = v * Rml::Vector3f( 0.272f, 0.534f, 0.131f );
		m = Rml::Matrix4f::FromRows( { rm.x + r, rm.y, rm.z, 0 }, { gm.x, gm.y + r, gm.z, 0 }, { bm.x, bm.y, bm.z + r, 0 }, { 0, 0, 0, 1 } );
	} else if ( name == "hue-rotate" ) {
		const float s = Rml::Math::Sin( value ), c = Rml::Math::Cos( value );
		m = Rml::Matrix4f::FromRows(
			{ 0.213f + 0.787f * c - 0.213f * s, 0.715f - 0.715f * c - 0.715f * s, 0.072f - 0.072f * c + 0.928f * s, 0 },
			{ 0.213f - 0.213f * c + 0.143f * s, 0.715f + 0.285f * c + 0.140f * s, 0.072f - 0.072f * c - 0.283f * s, 0 },
			{ 0.213f - 0.213f * c - 0.787f * s, 0.715f - 0.715f * c + 0.715f * s, 0.072f + 0.928f * c + 0.072f * s, 0 },
			{ 0, 0, 0, 1 } );
	} else if ( name == "saturate" ) {
		m = Rml::Matrix4f::FromRows(
			{ 0.213f + 0.787f * value, 0.715f - 0.715f * value, 0.072f - 0.072f * value, 0 },
			{ 0.213f - 0.213f * value, 0.715f + 0.285f * value, 0.072f - 0.072f * value, 0 },
			{ 0.213f - 0.213f * value, 0.715f - 0.715f * value, 0.072f + 0.928f * value, 0 },
			{ 0, 0, 0, 1 } );
	} else {
		Printf( "^3xenui: unknown filter '%s'\n", name.c_str() );
		return 0;
	}
	memcpy( f.matrix, m.data(), sizeof( f.matrix ) );
	return (Rml::CompiledFilterHandle)re->CompileFilter( &f );
}

// RmlUi's gradients; a custom "shader" decorator has nothing to run here
Rml::CompiledShaderHandle XenRender::CompileShader( const Rml::String &name, const Rml::Dictionary &parameters )
{
	xenUIShader_t s;

	if ( !advanced )
		return 0;
	memset( &s, 0, sizeof( s ) );
	if ( name == "linear-gradient" ) {
		const Rml::Vector2f p0 = Param( parameters, "p0", Rml::Vector2f( 0, 0 ) ), p1 = Param( parameters, "p1", Rml::Vector2f( 0, 0 ) );
		s.type = XENUI_SHADER_LINEAR;
		s.p[0] = p0.x;
		s.p[1] = p0.y;
		s.v[0] = p1.x - p0.x;
		s.v[1] = p1.y - p0.y;
	} else if ( name == "radial-gradient" ) {
		const Rml::Vector2f c = Param( parameters, "center", Rml::Vector2f( 0, 0 ) ), r = Param( parameters, "radius", Rml::Vector2f( 1, 1 ) );
		s.type = XENUI_SHADER_RADIAL;
		s.p[0] = c.x;
		s.p[1] = c.y;
		s.v[0] = r.x != 0 ? 1.0f / r.x : 0;
		s.v[1] = r.y != 0 ? 1.0f / r.y : 0;
	} else if ( name == "conic-gradient" ) {
		const Rml::Vector2f c = Param( parameters, "center", Rml::Vector2f( 0, 0 ) );
		const float angle = Param( parameters, "angle", 0.0f );
		s.type = XENUI_SHADER_CONIC;
		s.p[0] = c.x;
		s.p[1] = c.y;
		s.v[0] = Rml::Math::Cos( angle );
		s.v[1] = Rml::Math::Sin( angle );
	} else {
		Printf( "^3xenui: shader '%s' is not supported\n", name.c_str() );
		return 0;
	}
	s.repeating = Param( parameters, "repeating", false ) ? 1 : 0;
	auto it = parameters.find( "color_stop_list" );
	if ( it != parameters.end() && it->second.GetType() == Rml::Variant::COLORSTOPLIST ) {
		for ( const Rml::ColorStop &stop : it->second.GetReference<Rml::ColorStopList>() ) {
			if ( s.numStops >= XENUI_MAX_STOPS )
				break;
			s.stopColors[s.numStops][0] = stop.color.red / 255.0f;
			s.stopColors[s.numStops][1] = stop.color.green / 255.0f;
			s.stopColors[s.numStops][2] = stop.color.blue / 255.0f;
			s.stopColors[s.numStops][3] = stop.color.alpha / 255.0f;
			s.stopPositions[s.numStops] = stop.position.number;
			s.numStops++;
		}
	}
	if ( !s.numStops )
		return 0;
	return (Rml::CompiledShaderHandle)re->CompileShader( &s );
}

XenSystem			systemInterface;
XenFiles			fileInterface;
XenRender			renderInterface;
Rml::Context		*context;
Rml::Context		*underContext;	// underlays: drawn at the call, under what the screen draws next (its 3D stage)
double				underUpdate, underRender;	// this frame's underlay CPU (ui_xenStats adds it to the frame)
Rml::ElementDocument *document;
std::string			documentPath;
bool				debuggerReady;
int					width, height;

// CPU cost of Update + Render, averaged over the last STAT_FRAMES frames (the dump)
const int			STAT_FRAMES = 120;
double				statUpdate[STAT_FRAMES], statRender[STAT_FRAMES];
int					statFrame;

// every drawn frame since ui_xenStats reset (ui_xenStats); publish is the C screen's model sets, from its first set to
// its XenUI_Overlay call
struct FrameStat {
	double	publish, update, render, gpu;
	int		sets, changes;
};
std::vector<FrameStat>		windowFrames;
std::map<std::string, int>	windowDirty;	// model name: frames that dirtied it
int							windowDirtyFrames;
std::chrono::steady_clock::time_point publishStart, publishEnd;
bool						publishing, published;
int							frameSets, frameChanges;
std::set<std::string>		frameDirty;
std::vector<double>			windowUIFrame;	// XenUI_FrameMark: the UI's whole menu drawing, painted or RML
std::chrono::steady_clock::time_point uiFrameStart;

void PublishSet()
{
	if ( !publishing ) {
		publishing = true;
		publishStart = std::chrono::steady_clock::now();
	}
	frameSets++;
}

void PublishDirty( const std::string &top )
{
	frameChanges++;
	frameDirty.insert( top );
}

void StatsReset()
{
	windowFrames.clear();
	windowUIFrame.clear();
	windowDirty.clear();
	windowDirtyFrames = 0;
}

void StatsLine( const char *what, std::vector<double> v )
{
	double sum = 0;
	if ( v.empty() )
		return;
	for ( double x : v )
		sum += x;
	std::sort( v.begin(), v.end() );
	Printf( "xenui:   %-8s mean %.3f  median %.3f  p90 %.3f  max %.3f ms\n", what, sum / v.size(), v[v.size() / 2],
		v[v.size() * 9 / 10], v.back() );
}

void StatsPrint()
{
	std::vector<double> publish, update, render, total, gpu;
	long long sets = 0, changes = 0;

	for ( const FrameStat &f : windowFrames ) {
		publish.push_back( f.publish );
		update.push_back( f.update );
		render.push_back( f.render );
		total.push_back( f.publish + f.update + f.render );
		gpu.push_back( f.gpu );
		sets += f.sets;
		changes += f.changes;
	}
	Printf( "xenui: CPU over %d frames since ui_xenStats reset (%d UI frames)\n", (int)windowFrames.size(), (int)windowUIFrame.size() );
	StatsLine( "ui frame", windowUIFrame );
	StatsLine( "publish", publish );
	StatsLine( "update", update );
	StatsLine( "render", render );
	StatsLine( "total", total );
	if ( renderInterface.advanced )
		StatsLine( "gpu", gpu );	// xenui's own command buffer, a frame or two behind
	if ( windowFrames.empty() )
		return;
	Printf( "xenui:   sets %.1f, changed %.2f per frame; %d frames dirtied a name\n", (double)sets / windowFrames.size(),
		(double)changes / windowFrames.size(), windowDirtyFrames );
	for ( const auto &d : windowDirty )
		Printf( "xenui:     dirty %-12s %d frames\n", d.first.c_str(), d.second );
}

// ui_xenProfile [reset]: RmlUi's zones by self time per frame since the last reset (a XenUIProfile build)
void Profile( bool reset )
{
#ifdef RMLUI_TRACY_PROFILING
	std::vector<xenprof::Site *> list;
	for ( xenprof::Site *s = xenprof::sites; s; s = s->next ) {
		if ( reset )
			s->inclusive = s->self = 0, s->calls = 0;
		else if ( s->calls )
			list.push_back( s );
	}
	if ( reset ) {
		profileFrames = 0;
		return;
	}
	const double frames = profileFrames ? (double)profileFrames : 1;
	std::sort( list.begin(), list.end(), []( const xenprof::Site *a, const xenprof::Site *b ) { return a->self > b->self; } );
	Printf( "xenui: profile over %lld frames, ms per frame (self, inclusive), calls per frame\n", profileFrames );
	for ( size_t i = 0; i < list.size() && i < 40; i++ ) {
		const char *file = strrchr( list[i]->file, '\\' ) ? strrchr( list[i]->file, '\\' ) + 1 : list[i]->file;
		Printf( "xenui:   %7.4f %7.4f %8.1f  %s (%s:%d)\n", list[i]->self / frames, list[i]->inclusive / frames,
			list[i]->calls / frames, list[i]->name, file, list[i]->line );
	}
#else
	(void)reset;
	Printf( "xenui: ui_xenProfile needs a XenUIProfile build\n" );
#endif
}

const xenUIRenderAPI_t *FindRenderAPI()
{
	xenUIGetRenderAPI_t get = nullptr;

	if ( !host->rendererModule || !host->rendererModule[0] )
		return nullptr;
#ifdef _WIN32
	HMODULE module = GetModuleHandleA( host->rendererModule );
	if ( module )
		get = (xenUIGetRenderAPI_t)GetProcAddress( module, XENUI_RENDER_API_EXPORT );
#else
	void *module = dlopen( host->rendererModule, RTLD_LAZY | RTLD_NOLOAD );
	if ( module ) {
		get = (xenUIGetRenderAPI_t)dlsym( module, XENUI_RENDER_API_EXPORT );
		dlclose( module );
	}
#endif
	// this version, else version 1 (geometry, textures, scissor and transform only)
	const xenUIRenderAPI_t *api = get ? get( XENUI_RENDER_API_VERSION ) : nullptr;
	if ( !api && get )
		api = get( 1 );
	if ( !api || api->size < XENUI_RENDER_API_V1_SIZE )
		return nullptr;
	renderInterface.advanced = api->version >= 2 && api->size >= (int)sizeof( xenUIRenderAPI_t );
	return api;
}

void LoadFonts()
{
	static char	list[8192];
	const char	*exts[] = { ".ttf", ".otf" };

	for ( const char *ext : exts ) {
		const int count = host->FS_GetFileList( "fonts/xenui", ext, list, sizeof( list ) );
		const char *name = list;
		for ( int i = 0; i < count; i++, name += strlen( name ) + 1 ) {
			const std::string path = std::string( "fonts/xenui/" ) + name;
			if ( !Rml::LoadFontFace( path ) )
				Printf( "^3xenui: font %s did not load\n", path.c_str() );
		}
	}
}

void CloseDocument()
{
	if ( document ) {
		document->Close();
		document = nullptr;
		context->Update();	// Close takes effect on the next update
	}
	host->SetCatcher( 0 );
}

bool OpenDocument( const char *path )
{
	CloseDocument();
	document = context->LoadDocument( path );
	if ( !document ) {
		Printf( "^3xenui: %s did not load\n", path );
		return false;
	}
	documentPath = path;
	document->Show();
	host->SetCatcher( 1 );
	return true;
}

// --- dump -----------------------------------------------------------------------------------------------------------

void JsonString( std::string &out, const std::string &s )
{
	out += '"';
	for ( unsigned char c : s ) {
		if ( c == '"' || c == '\\' ) {
			out += '\\';
			out += (char)c;
		} else if ( c < 0x20 ) {
			char esc[8];
			snprintf( esc, sizeof( esc ), "\\u%04x", c );
			out += esc;
		} else {
			out += (char)c;
		}
	}
	out += '"';
}

void DumpElement( std::string &out, Rml::Element *el, int depth )
{
	char	num[160];
	const char *props[] = { "transform", "opacity", "color", "background-color", "font-family", "font-size", "nav-up", "nav-down" };

	out += "{\"tag\":";
	JsonString( out, el->GetTagName() );
	if ( !el->GetId().empty() ) {
		out += ",\"id\":";
		JsonString( out, el->GetId() );
	}
	if ( !el->GetClassNames().empty() ) {
		out += ",\"class\":";
		JsonString( out, el->GetClassNames() );
	}

	const Rml::Vector2f pos = el->GetAbsoluteOffset( Rml::BoxArea::Border );
	const Rml::Vector2f size = el->GetBox().GetSize( Rml::BoxArea::Border );
	snprintf( num, sizeof( num ), ",\"box\":[%.1f,%.1f,%.1f,%.1f],\"visible\":%s", pos.x, pos.y, size.x, size.y,
		el->IsVisible() ? "true" : "false" );
	out += num;

	std::string states;
	for ( const char *pseudo : { "hover", "focus", "active", "checked", "disabled" } ) {
		if ( el->IsPseudoClassSet( pseudo ) ) {
			if ( !states.empty() )
				states += ' ';
			states += pseudo;
		}
	}
	if ( !states.empty() ) {
		out += ",\"state\":";
		JsonString( out, states );
	}

	if ( Rml::ElementText *text = rmlui_dynamic_cast<Rml::ElementText *>( el ) ) {
		out += ",\"text\":";
		JsonString( out, text->GetText() );
	} else {
		std::string style;
		for ( const char *name : props ) {
			const Rml::Property *p = el->GetLocalProperty( name ) ? el->GetProperty( name ) : nullptr;
			if ( !p )
				continue;
			style += style.empty() ? "{" : ",";
			JsonString( style, name );
			style += ':';
			JsonString( style, p->ToString() );
		}
		if ( !style.empty() ) {
			out += ",\"style\":" + style + "}";
		}
	}

	if ( el->GetNumChildren() > 0 && depth < 64 ) {
		out += ",\"children\":[";
		for ( int i = 0; i < el->GetNumChildren(); i++ ) {
			if ( i )
				out += ',';
			DumpElement( out, el->GetChild( i ), depth + 1 );
		}
		out += ']';
	}
	out += '}';
}

void Averages( double *update, double *render )
{
	const int n = statFrame < STAT_FRAMES ? statFrame : STAT_FRAMES;
	double u = 0, r = 0;
	for ( int i = 0; i < n; i++ ) {
		u += statUpdate[i];
		r += statRender[i];
	}
	*update = n ? u / n : 0;
	*render = n ? r / n : 0;
}

// --- data models ----------------------------------------------------------------------------------------------------
//
// A model is a tree the C code fills by path ("rows[3].word", "status.warn"): structs, arrays and scalars made on first
// set, a top-level name bound on its first set (before the documents that use it load). A set that changes a value
// dirties its top-level name, so RmlUi updates only the views that read it. Events (data-event-click="row(3)") call
// the model's C handler with the event name, the arguments as strings and the mouse button.

struct XNode {
	enum Kind { SCALAR, ARRAY, STRUCT } kind = SCALAR;
	Rml::Variant								value;
	std::vector<std::unique_ptr<XNode>>			items;
	std::map<std::string, std::unique_ptr<XNode>>	fields;
	XNode										*sizeNode = nullptr;	// an array's "size"
};

XNode *ChildOf( void *ptr, const Rml::DataAddressEntry &address );

class XScalarDef : public Rml::VariableDefinition {
public:
	XScalarDef() : VariableDefinition( Rml::DataVariableType::Scalar ) {}
	bool Get( void *ptr, Rml::Variant &variant ) override { variant = ( (XNode *)ptr )->value; return true; }
	bool Set( void *ptr, const Rml::Variant &variant ) override { ( (XNode *)ptr )->value = variant; return true; }
};

class XArrayDef : public Rml::VariableDefinition {
public:
	XArrayDef() : VariableDefinition( Rml::DataVariableType::Array ) {}
	int Size( void *ptr ) override { return (int)( (XNode *)ptr )->items.size(); }
	Rml::DataVariable Child( void *ptr, const Rml::DataAddressEntry &address ) override;
};

class XStructDef : public Rml::VariableDefinition {
public:
	XStructDef() : VariableDefinition( Rml::DataVariableType::Struct ) {}
	Rml::DataVariable Child( void *ptr, const Rml::DataAddressEntry &address ) override;
};

XScalarDef	scalarDef;
XArrayDef	arrayDef;
XStructDef	structDef;

Rml::DataVariable VariableOf( XNode *node )
{
	if ( !node )
		return Rml::DataVariable();
	Rml::VariableDefinition *def = node->kind == XNode::ARRAY ? (Rml::VariableDefinition *)&arrayDef
		: node->kind == XNode::STRUCT ? (Rml::VariableDefinition *)&structDef : (Rml::VariableDefinition *)&scalarDef;
	return Rml::DataVariable( def, node );
}

Rml::DataVariable XArrayDef::Child( void *ptr, const Rml::DataAddressEntry &address )
{
	XNode *node = (XNode *)ptr;
	if ( address.index < 0 ) {
		if ( address.name == "size" ) {
			if ( !node->sizeNode ) {
				node->fields["size"].reset( new XNode );
				node->sizeNode = node->fields["size"].get();
			}
			node->sizeNode->value = (int)node->items.size();
			return VariableOf( node->sizeNode );
		}
		return Rml::DataVariable();
	}
	return address.index < (int)node->items.size() ? VariableOf( node->items[address.index].get() ) : Rml::DataVariable();
}

Rml::DataVariable XStructDef::Child( void *ptr, const Rml::DataAddressEntry &address )
{
	XNode *node = (XNode *)ptr;
	auto it = node->fields.find( address.name );
	return it != node->fields.end() ? VariableOf( it->second.get() ) : Rml::DataVariable();
}

struct XModel {
	std::string						name;
	Rml::DataModelConstructor		constructor;
	Rml::DataModelHandle			handle;
	std::map<std::string, std::unique_ptr<XNode>>	roots;
	xenUIEventFunc_t				onEvent = nullptr;
	// path -> scalar node and its top-level name, for bound names: a screen sets the same few hundred paths every
	// frame. Cleared when an array changes (its items may be freed).
	std::unordered_map<std::string, std::pair<XNode *, const std::string *>>	paths;
	std::string						key;	// the lookup key, reused so a lookup does not allocate
};

std::vector<std::unique_ptr<XModel>>	models;	// handle = index + 1

XModel *ModelOf( int model )
{
	return model > 0 && model <= (int)models.size() ? models[model - 1].get() : nullptr;
}

// the node at a path, made as needed; *top gets the top-level name
XNode *NodeAt( XModel *m, const char *path, std::string *top )
{
	const char	*p = path;
	std::string	name;

	while ( *p && *p != '.' && *p != '[' )
		name += *p++;
	if ( name.empty() )
		return nullptr;
	*top = name;

	auto &slot = m->roots[name];
	if ( !slot )
		slot.reset( new XNode );
	XNode *node = slot.get();

	while ( *p ) {
		if ( *p == '[' ) {
			const int index = atoi( ++p );
			while ( *p && *p != ']' )
				p++;
			if ( *p == ']' )
				p++;
			if ( index < 0 )
				return nullptr;
			node->kind = XNode::ARRAY;
			while ( (int)node->items.size() <= index )
				node->items.emplace_back( new XNode );
			node = node->items[index].get();
		} else if ( *p == '.' ) {
			std::string field;
			p++;
			while ( *p && *p != '.' && *p != '[' )
				field += *p++;
			node->kind = XNode::STRUCT;
			auto &child = node->fields[field];
			if ( !child )
				child.reset( new XNode );
			node = child.get();
		} else {
			return nullptr;
		}
	}
	return node;
}

// the cached scalar node at a path, or null (not cached yet)
XNode *CachedNode( XModel *m, const char *path, const std::string **top )
{
	m->key.assign( path );
	auto it = m->paths.find( m->key );
	if ( it == m->paths.end() || it->second.first->kind != XNode::SCALAR )
		return nullptr;
	*top = it->second.second;
	return it->second.first;
}

void SetValue( int model, const char *path, const Rml::Variant &value )
{
	XModel		*m = ModelOf( model );
	std::string	top;
	const std::string *cachedTop;

	if ( !m )
		return;
	PublishSet();
	if ( XNode *node = CachedNode( m, path, &cachedTop ) ) {
		if ( node->value == value )
			return;
		node->value = value;
		m->handle.DirtyVariable( *cachedTop );
		PublishDirty( *cachedTop );
		return;
	}
	const bool known = m->roots.count( std::string( path, strcspn( path, ".[" ) ) ) != 0;
	XNode *node = NodeAt( m, path, &top );
	if ( !node )
		return;
	if ( known )
		m->paths[path] = { node, &m->roots.find( top )->first };
	if ( node->kind == XNode::SCALAR && node->value == value && known )
		return;
	node->kind = XNode::SCALAR;
	node->value = value;
	if ( !known )	// a new top-level name: bound once its kind is set (the definition follows the kind)
		m->constructor.BindCustomDataVariable( top, VariableOf( m->roots[top].get() ) );
	m->handle.DirtyVariable( top );
	PublishDirty( top );
}

// a string set that leaves the value alone costs no Variant (and no allocation)
bool SameString( int model, const char *path, const char *value )
{
	XModel *m = ModelOf( model );
	const std::string *top;
	XNode *node = m ? CachedNode( m, path, &top ) : nullptr;

	if ( !node || node->value.GetType() != Rml::Variant::STRING || node->value.GetReference<Rml::String>() != value )
		return false;
	PublishSet();
	return true;
}

// --- overlays: documents a C screen draws over itself while it is open (it calls XenUI_Overlay every frame) ---------

struct XOverlay {
	std::string				path;
	Rml::ElementDocument	*doc = nullptr;
	bool					touched = false;
};

std::vector<XOverlay>	overlays;
std::vector<XOverlay>	underlays;

Rml::ElementDocument *ShownOverlay()
{
	for ( XOverlay &o : overlays )
		if ( o.doc && o.doc->IsVisible() )
			return o.doc;
	return nullptr;
}

bool Dump( const char *path )
{
	std::string	out;
	char		head[512];
	double		update, render;
	Rml::ElementDocument *shown = document ? document : ShownOverlay();	// an open document, else a screen's overlay

	Averages( &update, &render );
	snprintf( head, sizeof( head ), "{\"window\":[%d,%d],\"dp_ratio\":%.4f,\"cpu_ms\":{\"update\":%.4f,\"render\":%.4f,\"frames\":%d},\"document\":",
		width, height, context->GetDensityIndependentPixelRatio(), update, render, statFrame < STAT_FRAMES ? statFrame : STAT_FRAMES );
	out = head;
	JsonString( out, shown && shown != document ? shown->GetSourceURL() : documentPath );
	Rml::Element *focus = context->GetFocusElement();
	out += ",\"focus\":";
	if ( focus && focus != shown )
		JsonString( out, focus->GetAddress( false, false ) );
	else
		out += "null";
	out += ",\"tree\":";
	if ( shown )
		DumpElement( out, shown, 0 );
	else
		out += "null";
	out += "}\n";

	if ( host->FS_WriteFile( path, out.data(), (int)out.size() ) < 0 ) {
		Printf( "^3xenui: could not write %s\n", path );
		return false;
	}
	Printf( "xenui: wrote %s (%d bytes)\n", path, (int)out.size() );
	return true;
}

// --- input ----------------------------------------------------------------------------------------------------------

Rml::Input::KeyIdentifier KeyFor( int key )
{
	switch ( key ) {
	case A_CURSOR_UP:		return Rml::Input::KI_UP;
	case A_CURSOR_DOWN:		return Rml::Input::KI_DOWN;
	case A_CURSOR_LEFT:		return Rml::Input::KI_LEFT;
	case A_CURSOR_RIGHT:	return Rml::Input::KI_RIGHT;
	case A_ENTER:			return Rml::Input::KI_RETURN;
	case A_KP_ENTER:		return Rml::Input::KI_NUMPADENTER;
	case A_TAB:				return Rml::Input::KI_TAB;
	case A_SPACE:			return Rml::Input::KI_SPACE;
	case A_BACKSPACE:		return Rml::Input::KI_BACK;
	default:				return Rml::Input::KI_UNKNOWN;
	}
}

Rml::Input::KeyIdentifier KeyNamed( const char *name )
{
	static const struct { const char *name; Rml::Input::KeyIdentifier key; } names[] = {
		{ "up", Rml::Input::KI_UP }, { "down", Rml::Input::KI_DOWN }, { "left", Rml::Input::KI_LEFT },
		{ "right", Rml::Input::KI_RIGHT }, { "enter", Rml::Input::KI_RETURN }, { "tab", Rml::Input::KI_TAB },
		{ "space", Rml::Input::KI_SPACE },
	};
	for ( const auto &n : names )
		if ( !strcmp( n.name, name ) )
			return n.key;
	return Rml::Input::KI_UNKNOWN;
}

void PressKey( Rml::Input::KeyIdentifier key )
{
	context->ProcessKeyDown( key, 0 );
	if ( key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER )	// Enter clicks the focused element
		if ( Rml::Element *focus = context->GetFocusElement() )
			if ( focus != document )
				focus->Click();
	context->ProcessKeyUp( key, 0 );
}

} // namespace

extern "C" int XenUI_Init( const xenUIHost_t *h )
{
	host = h;
	re = FindRenderAPI();
	if ( !re ) {
		Printf( "xenui: the renderer (%s) has no xenui interface; stock menus only\n", host->rendererModule ? host->rendererModule : "?" );
		return 0;
	}

	re->GetWindowSize( &width, &height );
	Rml::SetSystemInterface( &systemInterface );
	Rml::SetFileInterface( &fileInterface );
	Rml::SetRenderInterface( &renderInterface );
	if ( !Rml::Initialise() ) {
		Printf( "^1xenui: RmlUi did not initialise\n" );
		re = nullptr;
		return 0;
	}
	LoadFonts();

	context = Rml::CreateContext( "xenui", Rml::Vector2i( width, height ) );
	if ( !context ) {
		Rml::Shutdown();
		re = nullptr;
		return 0;
	}
	context->SetDensityIndependentPixelRatio( height / 1080.0f );	// dp: the 1920 x 1080 design canvas
	underContext = Rml::CreateContext( "xenui_under", Rml::Vector2i( width, height ) );
	if ( underContext )
		underContext->SetDensityIndependentPixelRatio( height / 1080.0f );
	Printf( "xenui: RmlUi %s, %d x %d, dp %.3f\n", Rml::GetVersion().c_str(), width, height, height / 1080.0f );
	return 1;
}

extern "C" void XenUI_Shutdown( void )
{
	if ( !re )
		return;
	document = nullptr;
	overlays.clear();
	underlays.clear();
	if ( debuggerReady )
		Rml::Debugger::Shutdown();
	debuggerReady = false;
	Rml::Shutdown();	// releases every geometry and texture through the render interface
	models.clear();		// after the context: its data views read the nodes until it goes
	context = nullptr;
	underContext = nullptr;
	re = nullptr;
}

extern "C" int XenUI_Active( void )
{
	return re && document;
}

extern "C" void XenUI_Refresh( int realtime )
{
	bool shown = false;

	if ( !re )
		return;
	for ( XOverlay &o : overlays ) {	// an overlay shows while its screen asks for it each frame
		if ( o.doc && o.touched != o.doc->IsVisible() ) {
			if ( o.touched )
				o.doc->Show( Rml::ModalFlag::None, Rml::FocusFlag::None );
			else
				o.doc->Hide();
		}
		shown |= o.touched;
		o.touched = false;
	}
	const double publish = published ? std::chrono::duration<double, std::milli>( publishEnd - publishStart ).count() : 0;
	const int sets = frameSets, changes = frameChanges;
	publishing = published = false;
	frameSets = frameChanges = 0;
	if ( !document && !shown && !( debuggerReady && Rml::Debugger::IsVisible() ) ) {
		frameDirty.clear();
		underUpdate = underRender = 0;
		return;
	}

	XenUI_MouseMove();

	const auto t0 = std::chrono::steady_clock::now();
	context->Update();
	const auto t1 = std::chrono::steady_clock::now();
	context->Render();
	const auto t2 = std::chrono::steady_clock::now();

	re->SetScissor( 0, 0, 0, 0, 0 );
	re->SetTransform( nullptr );
	if ( renderInterface.advanced )
		re->EndFrame();	// the layers over the frame

	statUpdate[statFrame % STAT_FRAMES] = std::chrono::duration<double, std::milli>( t1 - t0 ).count() + underUpdate;
	statRender[statFrame % STAT_FRAMES] = std::chrono::duration<double, std::milli>( t2 - t1 ).count() + underRender;
	underUpdate = underRender = 0;
	if ( windowFrames.size() < 100000 )
		windowFrames.push_back( { publish, statUpdate[statFrame % STAT_FRAMES], statRender[statFrame % STAT_FRAMES],
			renderInterface.advanced ? re->GPUTime() : 0.0, sets, changes } );
	statFrame++;
	if ( !frameDirty.empty() )
		windowDirtyFrames++;
	for ( const std::string &name : frameDirty )
		windowDirty[name]++;
	frameDirty.clear();
#ifdef RMLUI_TRACY_PROFILING
	profileFrames++;
#endif
}

extern "C" void XenUI_FrameMark( int end )
{
	if ( !end ) {
		uiFrameStart = std::chrono::steady_clock::now();
	} else if ( windowUIFrame.size() < 100000 ) {
		windowUIFrame.push_back( std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - uiFrameStart ).count() );
	}
}

extern "C" void XenUI_MouseMove( void )
{
	static int lastX = -1, lastY = -1;
	const int x = host->CursorX(), y = host->CursorY();

	// only when the stock cursor moved, so a scripted hover (ui_xenSend <selector> hover) holds
	if ( re && context && ( x != lastX || y != lastY ) ) {
		lastX = x;
		lastY = y;
		context->ProcessMouseMove( x * width / 640, y * height / 480, 0 );
	}
}

extern "C" int XenUI_KeyEvent( int key, int down )
{
	if ( !XenUI_Active() )
		return 0;

	if ( key & K_CHAR_FLAG )
		return 1;	// no text entry yet

	if ( key == A_ESCAPE ) {
		if ( down )
			CloseDocument();
		return 1;
	}

	if ( key == A_MOUSE1 || key == A_MOUSE2 ) {
		context->ProcessMouseMove( host->CursorX() * width / 640, host->CursorY() * height / 480, 0 );
		if ( down )
			context->ProcessMouseButtonDown( key == A_MOUSE1 ? 0 : 1, 0 );
		else
			context->ProcessMouseButtonUp( key == A_MOUSE1 ? 0 : 1, 0 );
		return 1;
	}

	if ( key == A_MWHEELUP || key == A_MWHEELDOWN ) {
		if ( down )
			context->ProcessMouseWheel( key == A_MWHEELUP ? -1.0f : 1.0f, 0 );
		return 1;
	}

	const Rml::Input::KeyIdentifier k = KeyFor( key );
	if ( k != Rml::Input::KI_UNKNOWN ) {
		if ( down )
			PressKey( k );
	}
	return 1;
}

extern "C" int XenUI_ConsoleCommand( const char *cmd, int argc, const char **argv )
{
	std::string c( cmd );
	for ( char &ch : c )
		ch = (char)tolower( (unsigned char)ch );
	if ( c.compare( 0, 6, "ui_xen" ) )
		return 0;

	if ( !re ) {	// off (ui_xenui 0: no host either) or a renderer without the interface
		if ( host )
			Printf( "xenui: not available (the renderer has no xenui interface)\n" );
		return host != nullptr;
	}

	if ( c == "ui_xenopen" ) {
		if ( argc < 2 )
			Printf( "usage: ui_xenOpen <document.rml>\n" );
		else
			OpenDocument( argv[1] );
	} else if ( c == "ui_xenclose" ) {
		CloseDocument();
	} else if ( c == "ui_xenreload" ) {
		const std::string path = documentPath;
		Rml::Factory::ClearStyleSheetCache();
		Rml::Factory::ClearTemplateCache();
		if ( !path.empty() )
			OpenDocument( path.c_str() );
		for ( XOverlay &o : overlays ) {	// the screens' overlays too (hidden until their screen asks next frame)
			if ( o.doc )
				o.doc->Close();
			o.doc = context->LoadDocument( o.path );
			if ( o.doc )
				o.doc->Hide();
		}
		for ( XOverlay &o : underlays ) {
			if ( o.doc )
				o.doc->Close();
			o.doc = underContext->LoadDocument( o.path );
			if ( o.doc )
				o.doc->Hide();
		}
	} else if ( c == "ui_xendump" ) {
		Dump( argc >= 2 ? argv[1] : "xenui/dump.json" );
	} else if ( c == "ui_xenshot" ) {
		// the dump now and the screenshot of the next frame (the same state while nothing animates)
		const std::string name = argc >= 2 ? argv[1] : "xenui";
		Dump( ( "screenshots/" + name + ".json" ).c_str() );
		host->Cmd_Execute( ( "screenshot " + name + "\n" ).c_str() );
	} else if ( c == "ui_xensend" ) {
		Rml::ElementDocument *target = document ? document : ShownOverlay();
		Rml::Element *el = argc >= 3 && target ? target->QuerySelector( argv[1] ) : nullptr;
		const std::string what = argc >= 3 ? argv[2] : "";
		if ( !el ) {
			Printf( "^3xenui: usage ui_xenSend <selector> <click|focus|blur|hover|scrolltop>; no element for %s\n", argc >= 2 ? argv[1] : "" );
		} else if ( what == "click" ) {
			el->Click();
		} else if ( what == "focus" ) {
			el->Focus();
		} else if ( what == "blur" ) {
			el->Blur();
		} else if ( what == "scrolltop" ) {	// ui_xenSend <selector> scrolltop <dp>
			el->SetScrollTop( ( argc >= 4 ? (float)atof( argv[3] ) : 0.0f ) * context->GetDensityIndependentPixelRatio() );
		} else if ( what == "hover" ) {	// moves the pointer to the element's centre (the stock cursor stays where it is)
			const Rml::Vector2f p = el->GetAbsoluteOffset( Rml::BoxArea::Border ) + el->GetBox().GetSize( Rml::BoxArea::Border ) * 0.5f;
			context->ProcessMouseMove( (int)p.x, (int)p.y, 0 );
		} else {
			Printf( "^3xenui: unknown action %s\n", what.c_str() );
		}
	} else if ( c == "ui_xenkey" ) {
		const Rml::Input::KeyIdentifier k = argc >= 2 ? KeyNamed( argv[1] ) : Rml::Input::KI_UNKNOWN;
		if ( k == Rml::Input::KI_UNKNOWN )
			Printf( "usage: ui_xenKey <up|down|left|right|enter|tab|space>\n" );
		else if ( context )
			PressKey( k );
	} else if ( c == "ui_xendebug" ) {
		if ( !debuggerReady )
			debuggerReady = Rml::Debugger::Initialise( context );
		if ( debuggerReady )
			Rml::Debugger::SetVisible( argc >= 2 ? atoi( argv[1] ) != 0 : !Rml::Debugger::IsVisible() );
	} else if ( c == "ui_xenstats" ) {
		if ( argc >= 2 && !strcmp( argv[1], "reset" ) )
			StatsReset();
		else
			StatsPrint();
	} else if ( c == "ui_xenprofile" ) {
		Profile( argc >= 2 && !strcmp( argv[1], "reset" ) );
	} else {
		return 0;
	}
	return 1;
}

// --- data models and overlays (xenui.h) -----------------------------------------------------------------------------

static int ModelCreate( Rml::Context *ctx, const char *name, xenUIEventFunc_t onEvent )
{
	if ( !re || !ctx )
		return 0;
	for ( size_t i = 0; i < models.size(); i++ )
		if ( models[i]->name == name )
			return (int)i + 1;

	std::unique_ptr<XModel> m( new XModel );
	m->name = name;
	m->constructor = ctx->CreateDataModel( name, nullptr, true );	// a view of a name not set yet stays empty
	if ( !m->constructor )
		return 0;
	m->handle = m->constructor.GetModelHandle();
	m->onEvent = onEvent;
	models.push_back( std::move( m ) );
	return (int)models.size();
}

extern "C" int XenUI_ModelCreate( const char *name, xenUIEventFunc_t onEvent )
{
	return ModelCreate( context, name, onEvent );
}

extern "C" int XenUI_UnderlayModelCreate( const char *name )
{
	return ModelCreate( underContext, name, nullptr );
}

extern "C" void XenUI_ModelEvent( int model, const char *event )
{
	XModel *m = ModelOf( model );

	if ( !m )
		return;
	const std::string name( event );
	m->constructor.BindEventCallback( name, [m, name]( Rml::DataModelHandle, Rml::Event &ev, const Rml::VariantList &args ) {
		std::vector<std::string>	strings;
		const char					*argv[8];
		const int					argc = (int)( args.size() < 8 ? args.size() : 8 );

		for ( int i = 0; i < argc; i++ )
			strings.push_back( args[i].Get<Rml::String>() );
		for ( int i = 0; i < argc; i++ )
			argv[i] = strings[i].c_str();
		ev.StopPropagation();	// the innermost handler acts (an arrow inside a field, not the field too)
		if ( m->onEvent )
			m->onEvent( name.c_str(), argc, argv, ev.GetParameter<int>( "button", 0 ) );
	} );
}

extern "C" void XenUI_SetInt( int model, const char *path, int value ) { SetValue( model, path, Rml::Variant( value ) ); }
extern "C" void XenUI_SetFloat( int model, const char *path, float value ) { SetValue( model, path, Rml::Variant( value ) ); }
extern "C" void XenUI_SetBool( int model, const char *path, int value ) { SetValue( model, path, Rml::Variant( value != 0 ) ); }
extern "C" void XenUI_SetString( int model, const char *path, const char *value )
{
	if ( !SameString( model, path, value ? value : "" ) )
		SetValue( model, path, Rml::Variant( Rml::String( value ? value : "" ) ) );
}

extern "C" void XenUI_SetCount( int model, const char *path, int count )
{
	XModel		*m = ModelOf( model );
	std::string	top;

	if ( !m || count < 0 )
		return;
	PublishSet();
	const bool known = m->roots.count( std::string( path, strcspn( path, ".[" ) ) ) != 0;
	XNode *node = NodeAt( m, path, &top );
	if ( !node )
		return;
	if ( node->kind == XNode::ARRAY && (int)node->items.size() == count && known )
		return;
	node->kind = XNode::ARRAY;
	while ( (int)node->items.size() < count )
		node->items.emplace_back( new XNode );
	node->items.resize( count );
	m->paths.clear();	// freed items may be cached
	if ( !known )	// bound once its kind is set: the definition follows the kind at binding
		m->constructor.BindCustomDataVariable( top, VariableOf( m->roots[top].get() ) );
	m->handle.DirtyVariable( top );
	PublishDirty( top );
}

extern "C" int XenUI_Overlay( const char *path )
{
	if ( !re || !context )
		return 0;
	if ( publishing ) {
		publishEnd = std::chrono::steady_clock::now();
		published = true;
	}
	for ( XOverlay &o : overlays ) {
		if ( o.path == path ) {
			o.touched = o.doc != nullptr;
			return o.touched;
		}
	}
	XOverlay o;
	o.path = path;
	o.doc = context->LoadDocument( path );	// once: a document that fails is not retried until ui_xenReload
	if ( !o.doc )
		Printf( "^3xenui: overlay %s did not load\n", path );
	o.touched = o.doc != nullptr;
	overlays.push_back( o );
	return o.touched;
}

extern "C" int XenUI_Underlay( const char *path )
{
	XOverlay *u = nullptr;

	if ( !re || !underContext || !renderInterface.advanced )	// a version 1 renderer has one pass a frame
		return 0;
	for ( XOverlay &o : underlays ) {
		if ( o.path == path )
			u = &o;
	}
	if ( !u ) {
		XOverlay o;
		o.path = path;
		o.doc = underContext->LoadDocument( path );	// once, as an overlay
		if ( !o.doc )
			Printf( "^3xenui: underlay %s did not load\n", path );
		underlays.push_back( o );
		u = &underlays.back();
	}
	if ( !u->doc )
		return 0;
	for ( XOverlay &o : underlays ) {	// one underlay a frame: the one asked for
		if ( o.doc && ( &o == u ) != o.doc->IsVisible() ) {
			if ( &o == u )
				o.doc->Show( Rml::ModalFlag::None, Rml::FocusFlag::None );
			else
				o.doc->Hide();
		}
	}

	const auto t0 = std::chrono::steady_clock::now();
	underContext->Update();
	const auto t1 = std::chrono::steady_clock::now();
	underContext->Render();
	re->SetScissor( 0, 0, 0, 0, 0 );
	re->SetTransform( nullptr );
	re->EndFrame();	// this pass draws here, before the screen's stage
	const auto t2 = std::chrono::steady_clock::now();
	underUpdate += std::chrono::duration<double, std::milli>( t1 - t0 ).count();
	underRender += std::chrono::duration<double, std::milli>( t2 - t1 ).count();
	publishing = false;	// the overlay's publish starts at its own first set, not across the stage drawn between
	return 1;
}

extern "C" int XenUI_OverlayMouse( int key, int down )
{
	if ( !re || !context || !ShownOverlay() )
		return 0;
	context->ProcessMouseMove( host->CursorX() * width / 640, host->CursorY() * height / 480, 0 );
	if ( key == A_MOUSE1 || key == A_MOUSE2 ) {
		if ( down )
			context->ProcessMouseButtonDown( key == A_MOUSE1 ? 0 : 1, 0 );
		else
			context->ProcessMouseButtonUp( key == A_MOUSE1 ? 0 : 1, 0 );
		return 1;
	}
	return 0;
}
