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
Character texture streaming (docs/character-loading-plan.md, M3).

A character's DDS texture (a file under models/players/, mipmapped, larger than r_streamTailSize) loads only its mip
tail: R_LoadDDSImage reads the header and the levels at or below r_streamTailSize (~85 KB of a 4096^2 BC7 map's 22 MB)
and creates a small image of those levels, which stays for the image's life. The levels above it stream:

- Wanted level. Each frame the front end (R_StreamWantSurface, from the Ghoul2 surface adds) records, per image, the
  pixels per UV unit of the surfaces drawing it: the projection's pixels per world unit at the entity's distance times
  the surface's world units per UV unit (each triangle's largest stretch, held to 16:1 like anisotropic filtering; the
  area-weighted 90th percentile over its triangles, computed once). The
  wanted level is the smallest level with at least that many texels across, r_streamBias levels sharper; the largest
  value of the last 0.5-1 s counts. The model the client warms up (its own player, R_WarmModel) is pinned at full
  detail.
- Builds. A sharper image is a new VkImage of the levels from the wanted one down, in its own allocation: its first
  frame copies the levels already resident from the bound image on the GPU, then the missing levels arrive in slices
  of at most r_streamSliceMB (block rows of a level) that the engine's prefetch worker reads from the pk3 straight into
  a slot of a host-visible staging buffer (FS_RangeReadAsync); vk_begin_frame copies each arrived slice, at most
  r_streamUploadMB a frame, in a command buffer submitted ahead of the frame's own (R_StreamCmd). When the last slice is copied the image is
  transitioned and swapped in for that frame's draws: its own descriptor set replaces image->descriptor_set (a set
  bound by a frame in flight is never written). The replaced image is destroyed once the frames that drew it are done.
- Pool. Images sharper than their tail count against the pool: what the VRAM budget leaves (vk_vram.cpp) less
  r_vramReserveMB, at most r_streamPoolMB when that is set, read every frame. A build that does not fit first drops
  images no surface wanted lately back to their tails, least recently wanted first; if it still does not fit it builds
  a smaller level. When the pool shrinks under what it holds (the rest of the renderer grew: a model loaded), images
  not wanted lately go back to their tails that frame, so the budget's reserve is there for the next load.
- Pressure (R_StreamRelieve, docs/renderer-character-vram-20261109.md). Still over after that, the pool gives back in
  the order of what the screen loses: the emptiest block's images move into the other blocks' free ranges and the
  block is freed (nothing visible); drawn images two or more levels sharper than asked go to one above what is asked;
  then the squeeze, a pool-wide step at a time: first the spare levels, then a visible level each from every unpinned
  image. The squeeze falls again only after 5 s and when what it would restore fits with a tenth of the pool to spare,
  so a still view settles and stays.
- Whole-loaded textures (r_streamWhole). A player texture that cannot be read by range (a deflated DDS, or a PNG, JPG
  or TGA) is streamed too: its tail is made as any tail, its chain is uploaded once at registration into a pool block,
  it steps down and drops to its tail like the others, and comes back by being read and decoded whole again
  (R_StreamReloadStep, one a frame from RE_BeginFrame).

Everything else (world, UI, lightmaps, the render scene, loose or compressed files) loads as before.
*/
#include "tr_local.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

#define STREAM_MAX_LEVELS	16
#define STREAM_SLOTS		16
#define STREAM_BUILDS		8		// builds in progress at once (a render scene wants hundreds when the view jumps: 2 built about 100 a second)
#define STREAM_WINDOW_MS	500		// want windows: the larger of this one and the last counts

enum streamSlotState_t { SLOT_FREE, SLOT_READING, SLOT_READY, SLOT_COPIED };

struct streamImage_s {
	image_t			*image;
	int				file;						// FS_RangeOpen handle
	int				fileLength;					// its length; a range source reopens its path only to the same length
	bool			reopened;					// the handle was reopened once after a refused read
	int				width, height;				// chain level 0
	int				numLevels, tailLevel;		// the chain; its first resident tail level
	int				blockBytes;
	uint32_t		levelOffset[STREAM_MAX_LEVELS];	// in the file
	uint32_t		levelBytes[STREAM_MAX_LEVELS];
	bool			failed;						// failed for the map: stays as it is
	int				failures;					// failed allocations or reads so far (retried after 5 s each, failed at 4)

	// the tail, permanent
	VkImage			tailHandle;
	VkImageView		tailView;
	VkDescriptorSet	tailSet;

	// bound when sharper than the tail
	int				residentLevel;				// top chain level bound
	VkImage			handle;
	VkImageView		view;
	VkDescriptorSet	set;
	VkDeviceMemory	memory;
	VkDeviceSize	bytes;
	unsigned		boundSerial;				// frame serial the bound image first drew

	// a build: levels buildLevel.. into bHandle
	int				buildLevel;					// -1: none
	VkImage			bHandle;
	VkImageView		bView;
	VkDescriptorSet	bSet;
	VkDeviceMemory	bMemory;
	VkDeviceSize	bBytes;
	bool			bStarted;					// its first barriers and the resident levels' copy recorded
	int				nextLevel, nextRow;			// the next slice to read (chain level, block row)
	int				slicesOut;					// read or reading, not yet copied
	int				buildStarted;				// ri.Milliseconds()

	// want
	float			wantCur, wantPrev;			// pixels per UV unit, this window and the last
	int				wantedMs;					// last drawn
	int				pin;						// R_StreamPinBegin generation
	int				retryMs;					// no room in the pool: not before then
	bool			inWanting;					// in st.wanting

	// a whole-loaded texture (r_streamWhole): no range handle; read whole again to come back
	int				source;						// SRC_RANGE (path: the .dds name, for the reopen), SRC_DDS (path: the .dds file), SRC_RAW (path: R_LoadImage's name), SRC_ARRAY
	char			path[MAX_QPATH + 8];
	// SRC_ARRAY: a layered material's array (R_FindImageArray), RGBA8
	int				layers;						// array layers (else 0)
	char			files[PBR_MAX_LAYERS][MAX_QPATH];
	bool			arrayNormals;
	float			useScale;					// its texels per UV unit for one of the surface's (1 / the layers' smallest tile); 0: 1
};

enum { SRC_RANGE, SRC_DDS, SRC_RAW, SRC_ARRAY };
#define STREAM_SQUEEZE_MAX	5		// the spare levels, then four visible ones

struct streamSlot_t {
	streamSlotState_t	state;
	streamImage_s		*img;
	int					request;
	int					level, row, rows;
	uint32_t			bytes;
	unsigned			serial;					// copied in
};

struct streamDensity_t {
	int				numVerts, numTriangles;	// the surface it was measured on
	float			d;
};

struct streamRetired_t {
	VkImage			handle;
	VkImageView		view;
	VkDescriptorSet	set;
	VkDeviceMemory	memory;
	VkDeviceSize	bytes;						// (counted against the pool until destroyed)
	unsigned		serial;						// destroy from this serial on
};

static struct {
	std::vector<streamImage_s *>	images;
	std::vector<streamRetired_t>	retired;
	streamSlot_t					slot[STREAM_SLOTS];
	VkBuffer						staging;
	VkDeviceMemory					stagingMemory;
	byte							*stagingPtr;
	uint32_t						slotBytes;
	VkDescriptorPool				pool;
	VkCommandPool					commandPool;
	VkCommandBuffer					cmd[NUM_COMMAND_BUFFERS];
	bool							recording;
	unsigned						serial;
	int								windowMs;
	VkDeviceSize					poolBytes, usedBytes, tailBytes, retiredBytes;	// (retired: freed two frames on)
	double							uploadedMB;
	int								builds, evictions, stepDowns;
	int								pinGen, pinStarted;
	bool							pinReported;
	std::unordered_map<const shader_t *, std::vector<streamImage_s *> >	shaderImages;
	std::unordered_map<const void *, streamDensity_t>	density;
	std::vector<streamImage_s *>	building;	// the images with a build, in the order they began
	std::vector<streamImage_s *>	wanting;	// the images R_StreamStartBuilds looks at (their want or resident level moved)
	int								biasSeen;	// r_streamBias as last seen
	std::vector<streamImage_s *>	whole;		// the whole-loaded textures (R_StreamReloadStep looks at these)
	bool							poolCapped;	// the pool is r_streamPoolMB or its floor, not what the budget leaves
	int								squeeze, squeezeMs, easeMs;	// pressure steps in force; last change; last look at easing
	int								compactMs, compactions, trims, reloads;
	int								idleMs, idleDrops;			// r_streamIdleMB: last look; images it sent to their tails
	VkDeviceSize					idleBytes;					// what idle images held at that look
	double							reloadMs;
	VkDeviceSize					starvedBytes;	// a wanted build found no room last frame: this much
} st;

// an image whose wanted level may now be sharper than its resident one: looked at by the next R_StreamStartBuilds
static void R_StreamMark( streamImage_s *s ) {
	if ( !s->inWanting ) {
		s->inWanting = true;
		st.wanting.push_back( s );
	}
}

static void R_StreamBuildingRemove( streamImage_s *s ) {
	std::vector<streamImage_s *>::iterator it = std::find( st.building.begin(), st.building.end(), s );
	if ( it != st.building.end() ) {
		st.building.erase( it );
	}
}

static cvar_t *r_streamTextures, *r_streamTailSize, *r_streamPoolMB, *r_streamUploadMB, *r_streamSliceMB, *r_streamBias;

static cvar_t *r_streamStepDown, *r_streamWhole, *r_streamWholeIdle, *r_streamPinSize, *r_streamPinTeamSkins, *r_streamIdleMB;

static cvar_t *r_streamScene, *r_streamSceneTail;
static qboolean st_sceneImages;	// the scene being loaded streams its geometry (R_StreamSceneImages): its images stream too

void R_StreamRegister( void ) {
	r_streamScene = ri.Cvar_Get( "r_streamScene", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "Texture streaming: a render scene that streams its geometry (r_sceneStream) streams its images (scn/) too, each wanted at the on-screen size of the nearest LOD drawing it" );
	r_streamSceneTail = ri.Cvar_Get( "r_streamSceneTail", "128", CVAR_ARCHIVE_ND | CVAR_LATCH, "Texture streaming: the largest mip a render scene's streamed image loads with" );
	r_streamTextures = ri.Cvar_Get( "r_streamTextures", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "Character DDS textures load their small mips and stream the large ones by on-screen size (0 = load whole)" );
	r_streamTailSize = ri.Cvar_Get( "r_streamTailSize", "256", CVAR_ARCHIVE_ND | CVAR_LATCH, "Texture streaming: the largest mip a streamed texture loads with" );
	r_streamPoolMB = ri.Cvar_Get( "r_streamPoolMB", "0", CVAR_ARCHIVE_ND, "Texture streaming: the most VRAM for mips above the tails, in MB (0: what the VRAM budget leaves less r_vramReserveMB)" );
	r_streamUploadMB = ri.Cvar_Get( "r_streamUploadMB", "16", CVAR_ARCHIVE_ND, "Texture streaming: MB copied into streamed textures per frame" );
	r_streamSliceMB = ri.Cvar_Get( "r_streamSliceMB", "4", CVAR_ARCHIVE_ND | CVAR_LATCH, "Texture streaming: the largest read and copy, in MB (sixteen are staged)" );
	r_streamBias = ri.Cvar_Get( "r_streamBias", "0", CVAR_ARCHIVE_ND, "Texture streaming: levels sharper (positive) or softer than on-screen size asks" );
	r_streamStepDown = ri.Cvar_Get( "r_streamStepDown", "1", CVAR_ARCHIVE_ND, "Texture streaming: over the pool, drawn textures give levels back: blocks compacted, levels sharper than the screen asks, then a level from every unpinned texture at a time (0: only textures not drawn lately leave)" );
	r_streamWhole = ri.Cvar_Get( "r_streamWhole", "1", CVAR_ARCHIVE_ND | CVAR_LATCH, "Texture streaming: a character texture that cannot be read by range (a deflated DDS, PNG, JPG, TGA) is held in the pool too: it steps down and leaves like the others and is read and decoded whole again to come back (0: loaded whole for the map's life)" );
	r_streamWholeIdle = ri.Cvar_Get( "r_streamWholeIdle", "10", CVAR_ARCHIVE_ND, "Texture streaming: seconds a whole-loaded character texture (r_streamWhole) goes undrawn before it may go back to its tail (coming back is a decode)" );
	r_streamIdleMB = ri.Cvar_Get( "r_streamIdleMB", "2048", CVAR_ARCHIVE_ND, "Texture streaming: the most VRAM kept by textures nothing draws any more (the characters the picker moved past, players out of sight), in MB: past it the least recently drawn go back to their tails, whatever is free (0: kept until the pool needs the room)" );
	r_streamPinSize = ri.Cvar_Get( "r_streamPinSize", "0", CVAR_ARCHIVE_ND, "Texture streaming: the local player's textures are held at least this many texels across, sharper as the screen asks (0: full size)" );
	r_streamPinTeamSkins = ri.Cvar_Get( "r_streamPinTeamSkins", "0", CVAR_ARCHIVE_ND, "Texture streaming: the local player's red and blue team skins are pinned like the skin worn (0: warmed, then streamed by on-screen size when drawn)" );
}

qboolean R_StreamPinTeamSkins( void ) {
	return r_streamPinTeamSkins && r_streamPinTeamSkins->integer ? qtrue : qfalse;
}

// a texture that cannot stream by range but is held in the pool (r_streamWhole): a mipmapped character texture
qboolean R_StreamWholeWanted( const char *path, imgFlags_t flags ) {
	return vk.active && r_streamWhole && r_streamWhole->integer && r_streamTextures && r_streamTextures->integer && path
		&& !Q_stricmpn( path, "models/players/", 15 ) && ( flags & IMGFLAG_MIPMAP )
		&& !( flags & ( IMGFLAG_CUBEMAP | IMGFLAG_ARRAY | IMGFLAG_STORAGE | IMGFLAG_LIGHTMAP ) ) ? qtrue : qfalse;
}

static bool R_StreamHaveApi( void ) {
	return ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) >= 2 && ri.FS_RangeOpen && ri.FS_RangeReadAsync;
}

static qboolean R_StreamScenePath( const char *path ) {
	return st_sceneImages && r_streamScene && r_streamScene->integer && path && !Q_stricmpn( path, "scn/", 4 ) ? qtrue : qfalse;
}

qboolean R_StreamPath( const char *path ) {
	if ( R_StreamScenePath( path ) )
		return qtrue;
	return r_streamTextures && r_streamTextures->integer && path && !Q_stricmpn( path, "models/players/", 15 ) ? qtrue : qfalse;
}

// vk_scene.cpp, before a scene's images load: whether they stream (its geometry streams, so it says what it wants)
void R_StreamSceneImages( qboolean on ) {
	st_sceneImages = on;
}

int R_StreamTailBytes( void ) {
	const int t = r_streamTailSize ? MAX( r_streamTailSize->integer, 4 ) : 256;
	return 2 * t * t + 4096;	// a tail's levels at a byte per texel (BC3/5/7) with room to spare
}

int R_StreamTailSize( void ) {
	return r_streamTailSize ? MAX( r_streamTailSize->integer, 4 ) : 256;
}

int R_StreamOpen( const char *ddsName, const char *fileName, imgFlags_t flags, int *length ) {
	if ( !vk.active || !R_StreamPath( fileName ) || !R_StreamHaveApi() || !( flags & IMGFLAG_MIPMAP )
		|| ( flags & ( IMGFLAG_CUBEMAP | IMGFLAG_ARRAY | IMGFLAG_STORAGE ) ) ) {
		return 0;
	}
	return ri.FS_RangeOpen( ddsName, length );
}

// the chain level a streamed image's tail starts at; 0: the whole chain is a tail (not streamed). A scene's image has
// its own, smaller tail (20,000 of them load at once)
int R_StreamTailLevel( const char *path, int width, int height, int numLevels ) {
	const int t = R_StreamScenePath( path ) ? MAX( r_streamSceneTail->integer, 4 ) : R_StreamTailSize();
	int i = 0;
	while ( i < numLevels - 1 && MAX( width >> i, height >> i ) > t ) {
		i++;
	}
	return i;
}

static int R_StreamLevelDim( int d, int level ) {
	return MAX( d >> level, 1 );
}

// before vk_create_image makes the tail (it adds TRANSFER_SRC for the builds' copies). blockBytes 0: RGBA8
static streamImage_s *R_StreamAttachSource( image_t *image, int source, int file, const char *path, int width, int height,
	int numLevels, int tailLevel, int blockBytes, const uint32_t *levelOffset ) {
	streamImage_s *s = new streamImage_s;
	Com_Memset( s, 0, sizeof( *s ) );
	s->image = image;
	s->file = file;
	s->source = source;
	if ( path ) {
		Q_strncpyz( s->path, path, sizeof( s->path ) );
	}
	s->width = width;
	s->height = height;
	s->numLevels = MIN( numLevels, STREAM_MAX_LEVELS );
	s->tailLevel = tailLevel;
	s->blockBytes = blockBytes;
	for ( int i = 0; i < s->numLevels; i++ ) {
		s->levelOffset[i] = levelOffset[i];
		s->levelBytes[i] = blockBytes ? ( ( R_StreamLevelDim( width, i ) + 3 ) / 4 ) * ( ( R_StreamLevelDim( height, i ) + 3 ) / 4 ) * blockBytes
			: R_StreamLevelDim( width, i ) * R_StreamLevelDim( height, i ) * 4;
	}
	s->residentLevel = tailLevel;
	s->buildLevel = -1;
	s->wantedMs = ri.Milliseconds();
	image->stream = s;
	st.images.push_back( s );
	if ( source != SRC_RANGE ) {
		st.whole.push_back( s );
	}
	st.shaderImages.clear();	// a cached shader list may predate this image's shader
	return s;
}

void R_StreamAttach( image_t *image, int file, const char *ddsName, int fileLength, int width, int height, int numLevels,
	int tailLevel, int blockBytes, const uint32_t *levelOffset ) {
	streamImage_s *s = R_StreamAttachSource( image, SRC_RANGE, file, ddsName, width, height, numLevels, tailLevel, blockBytes,
		levelOffset );
	if ( s ) {
		s->fileLength = fileLength;
	}
}

// a whole-loaded texture (R_StreamWholeWanted): raw = RGBA8 from R_LoadImage( path ), else the block-compressed DDS
// file path; levelOffset: each chain level's offset, in the DDS file or in the raw chain
void R_StreamAttachWhole( image_t *image, qboolean raw, const char *path, int width, int height, int numLevels, int tailLevel,
	int blockBytes, const uint32_t *levelOffset ) {
	R_StreamAttachSource( image, raw ? SRC_RAW : SRC_DDS, 0, path, width, height, numLevels, tailLevel, raw ? 0 : blockBytes, levelOffset );
}

// a layered material's array (R_FindImageArray), whole-loaded like the above: count RGBA8 layers decoded from files.
// wantScale: what one UV unit of a surface drawing it is in the array's own (1 / the layers' smallest tile)
void R_StreamAttachArray( image_t *image, const char **files, int count, qboolean normals, int width, int height, int numLevels,
	int tailLevel, float wantScale ) {
	static const uint32_t none[STREAM_MAX_LEVELS] = { 0 };
	streamImage_s *s = R_StreamAttachSource( image, SRC_ARRAY, 0, NULL, width, height, numLevels, tailLevel, 0, none );
	s->layers = count < 1 ? 1 : count > PBR_MAX_LAYERS ? PBR_MAX_LAYERS : count;
	for ( int i = 0; i < s->numLevels; i++ ) {
		s->levelBytes[i] *= s->layers;
	}
	for ( int i = 0; i < s->layers; i++ ) {
		Q_strncpyz( s->files[i], files && files[i] && files[i][0] ? files[i] : "-", sizeof( s->files[i] ) );
	}
	s->arrayNormals = normals ? true : false;
	s->useScale = wantScale;
}

// the array is used again by a stage with other tiles: the largest scale counts
void R_StreamArrayScale( image_t *image, float wantScale ) {
	if ( image && image->stream && image->stream->source == SRC_ARRAY && wantScale > image->stream->useScale ) {
		image->stream->useScale = wantScale;
	}
}

// after the tail is uploaded: it is the image's permanent fallback
void R_StreamTailReady( image_t *image ) {
	streamImage_s *s = image->stream;
	if ( !s ) {
		return;
	}
	s->tailHandle = image->handle;
	s->tailView = image->view;
	s->tailSet = image->descriptor_set;
	for ( int i = s->tailLevel; i < s->numLevels; i++ ) {
		st.tailBytes += s->levelBytes[i];
	}
	R_StreamMark( s );
}

/*
=============================================================================

WANTED LEVELS (front end)

=============================================================================
*/

// world units per UV unit of a surface: the area-weighted 90th percentile of its triangles' largest stretch, the
// triangles that need the most texels for their size on screen; from at most STREAM_DENSITY_SAMPLES triangles, evenly
// strided (an unbiased estimate: a hero's 400-500k triangles and their sort took tens of ms in its first-draw frame)
#define STREAM_DENSITY_SAMPLES	4096
static float R_StreamSurfaceDensity( const mdxmSurface_t *surf ) {
	std::unordered_map<const void *, streamDensity_t>::iterator it = st.density.find( surf );
	if ( it != st.density.end() && it->second.numVerts == surf->numVerts && it->second.numTriangles == surf->numTriangles ) {
		return it->second.d;
	}
	const mdxmVertex_t *v = (const mdxmVertex_t *)( (const byte *)surf + surf->ofsVerts );
	const mdxmVertexTexCoord_t *tc = (const mdxmVertexTexCoord_t *)( v + surf->numVerts );
	const mdxmTriangle_t *t = (const mdxmTriangle_t *)( (const byte *)surf + surf->ofsTriangles );
	std::vector<std::pair<float, float> > tris;	// density, world area
	double total = 0;
	const int stride = MAX( 1, ( surf->numTriangles + STREAM_DENSITY_SAMPLES - 1 ) / STREAM_DENSITY_SAMPLES );
	const int t0 = ri.Milliseconds();
	tris.reserve( MIN( surf->numTriangles, STREAM_DENSITY_SAMPLES ) );
	for ( int i = 0; i < surf->numTriangles; i += stride ) {
		const int a = t[i].indexes[0], b = t[i].indexes[1], c = t[i].indexes[2];
		if ( a < 0 || b < 0 || c < 0 || a >= surf->numVerts || b >= surf->numVerts || c >= surf->numVerts ) {
			continue;
		}
		vec3_t e1, e2, n;
		VectorSubtract( v[b].vertCoords, v[a].vertCoords, e1 );
		VectorSubtract( v[c].vertCoords, v[a].vertCoords, e2 );
		CrossProduct( e1, e2, n );
		const float area = 0.5f * VectorLength( n );
		const float u1 = tc[b].texCoords[0] - tc[a].texCoords[0], v1 = tc[b].texCoords[1] - tc[a].texCoords[1];
		const float u2 = tc[c].texCoords[0] - tc[a].texCoords[0], v2 = tc[c].texCoords[1] - tc[a].texCoords[1];
		const float det = u1 * v2 - u2 * v1;
		if ( area <= 0.0f || fabsf( det ) < 2e-9f ) {
			continue;
		}
		// world units per UV unit along u and v (the mapping's Jacobian), then its largest stretch: a strand texture on
		// a thin card is sampled along its long axis (anisotropic filtering, up to 16:1), which an area ratio misses
		vec3_t ju, jv;
		for ( int k = 0; k < 3; k++ ) {
			ju[k] = ( e1[k] * v2 - e2[k] * v1 ) / det;
			jv[k] = ( e2[k] * u1 - e1[k] * u2 ) / det;
		}
		const float gu = DotProduct( ju, ju ), gv = DotProduct( jv, jv ), guv = DotProduct( ju, jv );
		const float mid = 0.5f * ( gu + gv ), rad = sqrtf( 0.25f * ( gu - gv ) * ( gu - gv ) + guv * guv );
		const float smax = sqrtf( mid + rad ), smin = sqrtf( MAX( mid - rad, 0.0f ) );
		tris.push_back( std::make_pair( MIN( smax, 16.0f * smin ), area ) );
		total += area;
	}
	float d = 0.0f;
	if ( !tris.empty() ) {
		std::sort( tris.begin(), tris.end() );
		double sum = 0;
		d = tris.back().first;
		for ( size_t i = 0; i < tris.size(); i++ ) {
			sum += tris[i].second;
			if ( sum >= 0.9 * total ) {
				d = tris[i].first;
				break;
			}
		}
	}
	const int ms = ri.Milliseconds() - t0;
	if ( ms > 2 ) {
		ri.Printf( PRINT_DEVELOPER, "stream: a surface's texel density (%d triangles, every %d) took %d ms\n", surf->numTriangles, stride, ms );
	}
	streamDensity_t rec;
	rec.numVerts = surf->numVerts;
	rec.numTriangles = surf->numTriangles;
	rec.d = d;
	st.density[surf] = rec;
	return d;
}

static void R_StreamAddImage( std::vector<streamImage_s *> &list, const image_t *image ) {
	if ( image && image->stream && std::find( list.begin(), list.end(), image->stream ) == list.end() ) {
		list.push_back( image->stream );
	}
}

static const std::vector<streamImage_s *> &R_StreamShaderImages( const shader_t *shader ) {
	std::unordered_map<const shader_t *, std::vector<streamImage_s *> >::iterator it = st.shaderImages.find( shader );
	if ( it != st.shaderImages.end() ) {
		return it->second;
	}
	std::vector<streamImage_s *> &list = st.shaderImages[shader];
	for ( int s = 0; s < MAX_SHADER_STAGES; s++ ) {
		const shaderStage_t *stage = shader->stages[s];
		if ( !stage || !stage->active ) {
			break;
		}
		for ( int b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
			const int n = MAX( (int)stage->bundle[b].numImageAnimations, 1 );
			for ( int i = 0; i < n && i < MAX_IMAGE_ANIMATIONS; i++ ) {
				R_StreamAddImage( list, stage->bundle[b].image[i] );
			}
		}
		R_StreamAddImage( list, stage->normalMap );
		R_StreamAddImage( list, stage->physicalMap );
#ifdef USE_VK_PBR
		R_StreamAddImage( list, stage->mat.detailNormalMap );
		R_StreamAddImage( list, stage->mat.maskMap );
		R_StreamAddImage( list, stage->mat.detailMap );
		R_StreamAddImage( list, stage->mat.emissiveMap );
		R_StreamAddImage( list, stage->mat.tintMap );
		R_StreamAddImage( list, stage->mat.toonShadeMap );
		R_StreamAddImage( list, stage->mat.fmaMaskMap );
		R_StreamAddImage( list, stage->mat.fmaVertexMap );
		R_StreamAddImage( list, stage->mat.fmaDetailMap );
		R_StreamAddImage( list, stage->mat.fmaFaceMap );
		R_StreamAddImage( list, stage->mat.fmaRampMap );
		R_StreamAddImage( list, stage->mat.fmaRimRampMap );
		R_StreamAddImage( list, stage->mat.fmaSSSRampMap );
		R_StreamAddImage( list, stage->mat.layerMask );
		R_StreamAddImage( list, stage->mat.layerAlbedo );
		R_StreamAddImage( list, stage->mat.layerNormal );
#endif
	}
	return list;
}

// a Ghoul2 surface added to the view: its shader's streamed images want its pixels per UV unit
void R_StreamWantSurface( const shader_t *shader, const trRefEntity_t *ent, const mdxmSurface_t *surf ) {
	if ( st.images.empty() || !shader || !surf ) {
		return;
	}
	if ( tr.viewParms.sunCascade || tr.viewParms.charShadow ) {
		return;	// (a shadow view's fov 90 at the map's size is not the screen's: it wanted a level or two sharper)
	}
	if ( shader->remappedShader ) {
		shader = shader->remappedShader;
	}
	const std::vector<streamImage_s *> &list = R_StreamShaderImages( shader );
	if ( list.empty() ) {
		return;
	}
	vec3_t delta;
	VectorSubtract( ent->e.origin, tr.viewParms.ori.origin, delta );
	const float dist = MAX( VectorLength( delta ), 16.0f );
	float scale = MAX( MAX( ent->e.modelScale[0], ent->e.modelScale[1] ), ent->e.modelScale[2] );
	if ( scale <= 0.0f ) {
		scale = 1.0f;
	}
	const float fovY = tr.viewParms.fovY > 1.0f ? tr.viewParms.fovY : 90.0f;
	const float pixelsPerUnit = tr.viewParms.viewportHeight / ( 2.0f * tanf( DEG2RAD( fovY ) * 0.5f ) ) / dist;
	const float want = R_StreamSurfaceDensity( surf ) * scale * pixelsPerUnit;
	const int now = ri.Milliseconds();
	for ( size_t i = 0; i < list.size(); i++ ) {
		streamImage_s *s = list[i];
		s->wantCur = MAX( s->wantCur, s->useScale > 0.0f ? want * s->useScale : want );	// (a tiled layer array: per repeat)
		s->wantedMs = now;
		R_StreamMark( s );
	}
}

// the render scene (vk_scene.cpp's geometry streaming): a streamed image wants this many texels per UV unit
void R_StreamWantImage( image_t *image, float want ) {
	if ( !image || !image->stream || want <= 0.0f ) {
		return;
	}
	streamImage_s *s = image->stream;
	s->wantCur = MAX( s->wantCur, want );
	s->wantedMs = ri.Milliseconds();
	R_StreamMark( s );
}

void R_StreamPinBegin( int gen ) {
	st.pinGen = gen;
	st.pinStarted = ri.Milliseconds();
	st.pinReported = false;
}

void R_StreamPinShader( const shader_t *shader, int gen ) {
	if ( st.images.empty() || !shader || gen != st.pinGen ) {
		return;
	}
	const std::vector<streamImage_s *> &list = R_StreamShaderImages( shader->remappedShader ? shader->remappedShader : shader );
	for ( size_t i = 0; i < list.size(); i++ ) {
		list[i]->pin = gen;
		list[i]->wantedMs = ri.Milliseconds();
		R_StreamMark( list[i] );
	}
}

static bool R_StreamPinned( const streamImage_s *s ) {
	return st.pinGen && s->pin == st.pinGen;
}

// the level an image should have bound now; tailLevel when nothing drew it lately. The squeeze (R_StreamRelieve) adds
// its visible steps; the local player's pinned textures take none and never go under r_streamPinSize
static int R_StreamWantLevel( const streamImage_s *s ) {
	const float want = MAX( s->wantCur, s->wantPrev );
	const bool pinned = R_StreamPinned( s );
	if ( pinned && r_streamPinSize->integer <= 0 ) {
		return 0;
	}
	if ( want <= 0.0f && !pinned ) {
		return s->tailLevel;
	}
	int level = s->tailLevel;
	if ( want > 0.0f ) {
		const float size = (float)MAX( s->width, s->height );
		level = 0;
		while ( level < s->tailLevel && size / (float)( 1 << ( level + 1 ) ) >= want ) {
			level++;
		}
		level -= r_streamBias->integer;
		if ( !pinned && st.squeeze > 1 ) {
			level += st.squeeze - 1;
		}
	}
	if ( pinned ) {
		int floor = 0;
		while ( floor < s->tailLevel && ( MAX( s->width, s->height ) >> ( floor + 1 ) ) >= r_streamPinSize->integer ) {
			floor++;
		}
		level = MIN( level, floor );
	}
	return level < 0 ? 0 : level > s->tailLevel ? s->tailLevel : level;
}

// not drawn lately: may go back to its tail to make room. A whole-loaded texture only after r_streamWholeIdle seconds
// (coming back is a decode), or at once under the squeeze
static bool R_StreamIdle( const streamImage_s *s, int now ) {
	if ( R_StreamWantLevel( s ) != s->tailLevel ) {
		return false;
	}
	if ( s->source == SRC_RANGE || st.squeeze > 0 ) {
		return true;
	}
	return now < s->wantedMs || now - s->wantedMs >= 1000 * MAX( r_streamWholeIdle->integer, 1 );
}

/*
=============================================================================

GPU SIDE (back end, vk_begin_frame)

=============================================================================
*/

// the pool: what the VRAM budget leaves (vk_vram.cpp: the device-local heap's budget less the process's use) once the
// rest of the renderer and r_vramReserveMB are taken out, at most r_streamPoolMB when that is set; 128 MB at least
// "Ours" is the blocks the built images live in (R_StreamBlockBytes), free ranges included: those are the pool's to
// fill, and R_StreamRelieve compacts them away when the blocks themselves are over
static VkDeviceSize R_StreamBlockBytes( VkDeviceSize *inUse );

static VkDeviceSize R_StreamPoolBytes( void ) {
	VkDeviceSize usage;
	const VkDeviceSize budget = vk_vram_budget( &usage );
	const VkDeviceSize ours = MAX( R_StreamBlockBytes( NULL ), st.usedBytes + st.retiredBytes );
	const VkDeviceSize others = usage > ours ? usage - ours : 0, keep = vk_vram_reserve();
	VkDeviceSize pool = budget > others + keep ? budget - others - keep : 0;
	st.poolCapped = pool < ( (VkDeviceSize)128 << 20 );
	pool = MAX( pool, (VkDeviceSize)128 << 20 );
	if ( r_streamPoolMB->integer > 0 && ( (VkDeviceSize)r_streamPoolMB->integer << 20 ) < pool ) {
		pool = (VkDeviceSize)r_streamPoolMB->integer << 20;
		st.poolCapped = true;
	}
	return pool;
}

static bool R_StreamInitGpu( void ) {
	if ( st.staging ) {
		return true;
	}
	st.slotBytes = (uint32_t)MAX( 1, MIN( r_streamSliceMB->integer, 64 ) ) << 20;
	VkBufferCreateInfo bi;
	Com_Memset( &bi, 0, sizeof( bi ) );
	bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bi.size = (VkDeviceSize)st.slotBytes * STREAM_SLOTS;
	bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CREATE_BUFFER( vk.device, &bi, &st.staging, "stream staging" );
	VkMemoryRequirements req;
	qvkGetBufferMemoryRequirements( vk.device, st.staging, &req );
	VkMemoryAllocateInfo ai;
	Com_Memset( &ai, 0, sizeof( ai ) );
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	if ( VK_ALLOCATE_MEMORY( vk.device, &ai, &st.stagingMemory, "stream staging" ) != VK_SUCCESS ) {
		VK_DESTROY_BUFFER( vk.device, st.staging );
		st.staging = VK_NULL_HANDLE;
		st.stagingMemory = VK_NULL_HANDLE;
		return false;
	}
	VK_CHECK( qvkBindBufferMemory( vk.device, st.staging, st.stagingMemory, 0 ) );
	VK_CHECK( qvkMapMemory( vk.device, st.stagingMemory, 0, VK_WHOLE_SIZE, 0, (void **)&st.stagingPtr ) );
	VK_SET_OBJECT_NAME( st.staging, "stream staging", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );

	VkDescriptorPoolSize ps;
	ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ps.descriptorCount = 32768;	// a set per built image: a render scene builds thousands (4,096 ran out)
	VkDescriptorPoolCreateInfo pi;
	Com_Memset( &pi, 0, sizeof( pi ) );
	pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pi.maxSets = ps.descriptorCount;
	pi.poolSizeCount = 1;
	pi.pPoolSizes = &ps;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &pi, NULL, &st.pool ) );
	for ( int i = 0; i < STREAM_SLOTS; i++ ) {
		st.slot[i].state = SLOT_FREE;
	}

	VkCommandPoolCreateInfo cp;
	Com_Memset( &cp, 0, sizeof( cp ) );
	cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp.queueFamilyIndex = vk.queue_family_index;
	VK_CHECK( qvkCreateCommandPool( vk.device, &cp, NULL, &st.commandPool ) );
	VkCommandBufferAllocateInfo ca;
	Com_Memset( &ca, 0, sizeof( ca ) );
	ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ca.commandPool = st.commandPool;
	ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ca.commandBufferCount = NUM_COMMAND_BUFFERS;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &ca, st.cmd ) );
	return true;
}

/*
The streaming commands go in a command buffer of their own, submitted at once from vk_begin_frame: queue order puts
them before the frame's draws (the barriers' second scope covers later submissions), and a frame vk_end_frame drops
(a geometry buffer resize) cannot lose them while the CPU side counts them as done. Buffer serial % 2 is reused two
serials later, after vk_begin_frame waited for the frame submitted after it.
*/
static VkCommandBuffer R_StreamCmd( void ) {
	VkCommandBuffer cmd = st.cmd[st.serial % NUM_COMMAND_BUFFERS];
	if ( !st.recording ) {
		VkCommandBufferBeginInfo bi;
		Com_Memset( &bi, 0, sizeof( bi ) );
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK( qvkResetCommandBuffer( cmd, 0 ) );
		VK_CHECK( qvkBeginCommandBuffer( cmd, &bi ) );
		st.recording = true;
	}
	return cmd;
}

static void R_StreamSubmit( void ) {
	if ( !st.recording ) {
		return;
	}
	VkCommandBuffer cmd = st.cmd[st.serial % NUM_COMMAND_BUFFERS];
	VK_CHECK( qvkEndCommandBuffer( cmd ) );
	VkSubmitInfo si;
	Com_Memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &si, VK_NULL_HANDLE ) );
	st.recording = false;
}

/*
Built images' memory: blocks of STREAM_BLOCK_BYTES of one memory type, each image a range of one (first fit, merged when
freed; an image larger than a block gets a block its size). One allocation per image ran into the driver's limit
(4,096 on Windows) once a render scene's images stream: every build after that failed.
*/
#define STREAM_BLOCK_BYTES	( (VkDeviceSize)64 << 20 )
struct streamBlock_t {
	VkDeviceMemory						memory;
	uint32_t							type;
	VkDeviceSize						size, used;
	std::map<VkDeviceSize, VkDeviceSize>	freeRanges;	// offset -> length
};
struct streamRange_t { size_t block; VkDeviceSize offset, size; };
static std::vector<streamBlock_t>						st_blocks;
static std::unordered_map<VkImage, streamRange_t>		st_ranges;	// a built image's range, by its handle

static bool R_StreamBlockFit( streamBlock_t &b, const VkMemoryRequirements &req, VkDeviceSize *offset ) {
	for ( std::map<VkDeviceSize, VkDeviceSize>::iterator it = b.freeRanges.begin(); it != b.freeRanges.end(); ++it ) {
		const VkDeviceSize at = ( it->first + req.alignment - 1 ) / req.alignment * req.alignment;
		if ( at + req.size > it->first + it->second ) {
			continue;
		}
		const VkDeviceSize start = it->first, end = it->first + it->second;
		b.freeRanges.erase( it );
		if ( at > start ) {
			b.freeRanges[start] = at - start;
		}
		if ( end > at + req.size ) {
			b.freeRanges[at + req.size] = end - ( at + req.size );
		}
		b.used += req.size;
		*offset = at;
		return true;
	}
	return false;
}

// the blocks' bytes; *inUse: the ranges taken in them (built, building and retired images)
static VkDeviceSize R_StreamBlockBytes( VkDeviceSize *inUse ) {
	VkDeviceSize size = 0, used = 0;
	for ( size_t i = 0; i < st_blocks.size(); i++ ) {
		if ( st_blocks[i].memory ) {
			size += st_blocks[i].size;
			used += st_blocks[i].used;
		}
	}
	if ( inUse ) {
		*inUse = used;
	}
	return size;
}

// the image bound to a range of a block (a new block when none has room); *memory: its block's (never freed by it).
// avoid >= 0: not in that block and in no new one (compaction: false when the others have no range for it)
static void R_StreamReleaseImage( VkImage handle );
static bool R_StreamBindImage( VkImage handle, VkDeviceMemory *memory, int avoid = -1 ) {
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, handle, &req );
	const uint32_t type = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	streamRange_t r;
	r.size = req.size;
	size_t i = 0;
	for ( ; i < st_blocks.size(); i++ ) {
		if ( (int)i != avoid && st_blocks[i].memory && st_blocks[i].type == type && R_StreamBlockFit( st_blocks[i], req, &r.offset ) ) {
			break;
		}
	}
	if ( i == st_blocks.size() && avoid >= 0 ) {
		return false;
	}
	if ( i == st_blocks.size() ) {
		streamBlock_t b;
		b.type = type;
		b.size = MAX( STREAM_BLOCK_BYTES, req.size );
		b.used = 0;
		b.memory = VK_NULL_HANDLE;
		VkMemoryAllocateInfo ai;
		Com_Memset( &ai, 0, sizeof( ai ) );
		ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize = b.size;
		ai.memoryTypeIndex = type;
		if ( VK_ALLOCATE_MEMORY( vk.device, &ai, &b.memory, "stream images" ) != VK_SUCCESS ) {
			return false;
		}
		b.freeRanges[0] = b.size;
		for ( i = 0; i < st_blocks.size() && st_blocks[i].memory; i++ ) {
		}
		if ( i == st_blocks.size() ) {
			st_blocks.push_back( b );
		} else {
			st_blocks[i] = b;
		}
		if ( !R_StreamBlockFit( st_blocks[i], req, &r.offset ) ) {
			return false;
		}
	}
	r.block = i;
	st_ranges[handle] = r;
	if ( qvkBindImageMemory( vk.device, handle, st_blocks[i].memory, r.offset ) != VK_SUCCESS ) {
		R_StreamReleaseImage( handle );	// the range back to its block
		return false;
	}
	*memory = st_blocks[i].memory;
	return true;
}

// a built image's range back to its block (merged with its neighbours); an empty block is freed
static void R_StreamReleaseImage( VkImage handle ) {
	std::unordered_map<VkImage, streamRange_t>::iterator it = st_ranges.find( handle );
	if ( it == st_ranges.end() ) {
		return;
	}
	streamBlock_t &b = st_blocks[it->second.block];
	VkDeviceSize at = it->second.offset, n = it->second.size;
	b.used -= n;
	st_ranges.erase( it );
	std::map<VkDeviceSize, VkDeviceSize>::iterator next = b.freeRanges.lower_bound( at );
	if ( next != b.freeRanges.begin() ) {
		std::map<VkDeviceSize, VkDeviceSize>::iterator prev = std::prev( next );
		if ( prev->first + prev->second >= at ) {
			n = MAX( prev->first + prev->second, at + n ) - prev->first;
			at = prev->first;
			b.freeRanges.erase( prev );
		}
	}
	next = b.freeRanges.lower_bound( at );
	while ( next != b.freeRanges.end() && next->first <= at + n ) {
		n = MAX( at + n, next->first + next->second ) - at;
		next = b.freeRanges.erase( next );
	}
	b.freeRanges[at] = n;
	if ( !b.used ) {
		VK_FREE_MEMORY( vk.device, b.memory );
		b.memory = VK_NULL_HANDLE;
		b.freeRanges.clear();
	}
}

static void R_StreamDestroy( VkImage handle, VkImageView view, VkDescriptorSet set, VkDeviceMemory memory ) {
	if ( view ) qvkDestroyImageView( vk.device, view, NULL );
	const bool ranged = handle && st_ranges.count( handle );
	if ( handle ) qvkDestroyImage( vk.device, handle, NULL );
	if ( set ) qvkFreeDescriptorSets( vk.device, st.pool, 1, &set );
	if ( ranged )
		R_StreamReleaseImage( handle );	// (after the image: its range is free once nothing is bound to it)
	else if ( memory )
		VK_FREE_MEMORY( vk.device, memory );
}

static void R_StreamRetire( VkImage handle, VkImageView view, VkDescriptorSet set, VkDeviceMemory memory, VkDeviceSize bytes ) {
	streamRetired_t r;
	r.handle = handle;
	r.view = view;
	r.set = set;
	r.memory = memory;
	r.bytes = bytes;
	st.retiredBytes += bytes;
	r.serial = st.serial + NUM_COMMAND_BUFFERS;
	st.retired.push_back( r );
}

static void R_StreamBarrier( VkCommandBuffer cmd, VkImage image, uint32_t baseLevel, uint32_t levels, VkImageLayout from,
	VkImageLayout to, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage ) {
	VkImageMemoryBarrier b;
	Com_Memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	b.subresourceRange.baseMipLevel = baseLevel;
	b.subresourceRange.levelCount = levels;
	b.subresourceRange.baseArrayLayer = 0;
	b.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	qvkCmdPipelineBarrier( cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b );
}

static VkDeviceSize R_StreamLevelsBytes( const streamImage_s *s, int top ) {
	VkDeviceSize n = 0;
	for ( int i = top; i < s->numLevels; i++ ) {
		n += s->levelBytes[i];
	}
	return n;
}

// a VkImage of chain levels top.. in a range of a block, with a descriptor set from the streaming pool (avoid: see
// R_StreamBindImage)
static bool R_StreamCreateBuild( streamImage_s *s, int top, int avoid = -1 ) {
	image_t *image = s->image;
	VkImageCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ci.imageType = VK_IMAGE_TYPE_2D;
	ci.format = (VkFormat)image->internalFormat;
	ci.extent.width = R_StreamLevelDim( s->width, top );
	ci.extent.height = R_StreamLevelDim( s->height, top );
	ci.extent.depth = 1;
	ci.mipLevels = s->numLevels - top;
	ci.arrayLayers = MAX( s->layers, 1 );
	ci.samples = VK_SAMPLE_COUNT_1_BIT;
	ci.tiling = VK_IMAGE_TILING_OPTIMAL;
	ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImage handle = VK_NULL_HANDLE;
	if ( qvkCreateImage( vk.device, &ci, NULL, &handle ) != VK_SUCCESS ) {
		return false;
	}
	VkMemoryRequirements req;
	qvkGetImageMemoryRequirements( vk.device, handle, &req );
	VkDeviceMemory memory = VK_NULL_HANDLE;
	if ( !R_StreamBindImage( handle, &memory, avoid ) ) {	// a range of a block (STREAM_BLOCK_BYTES)
		qvkDestroyImage( vk.device, handle, NULL );
		return false;
	}

	VkImageViewCreateInfo vi;
	Com_Memset( &vi, 0, sizeof( vi ) );
	vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vi.image = handle;
	vi.viewType = ( image->flags & IMGFLAG_ARRAY ) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	vi.format = ci.format;
	vi.components = textureMapTypes[image->type].swizzle;
	vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	vi.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
	vi.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
	VkImageView view = VK_NULL_HANDLE;
	VK_CHECK( qvkCreateImageView( vk.device, &vi, NULL, &view ) );

	VkDescriptorSetAllocateInfo di;
	Com_Memset( &di, 0, sizeof( di ) );
	di.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	di.descriptorPool = st.pool;
	di.descriptorSetCount = 1;
	di.pSetLayouts = &vk.set_layout_sampler;
	VkDescriptorSet set = VK_NULL_HANDLE;
	if ( qvkAllocateDescriptorSets( vk.device, &di, &set ) != VK_SUCCESS ) {
		R_StreamDestroy( handle, view, VK_NULL_HANDLE, memory );
		return false;
	}
	vk_write_image_descriptor( set, view, image, qtrue );
	VK_SET_OBJECT_NAME( handle, image->imgName, VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT );

	s->buildLevel = top;
	s->bHandle = handle;
	s->bView = view;
	s->bSet = set;
	s->bMemory = memory;
	s->bBytes = req.size;
	s->bStarted = false;
	s->nextLevel = top;
	s->nextRow = 0;
	s->slicesOut = 0;
	s->buildStarted = ri.Milliseconds();
	st.usedBytes += req.size;
	st.builds++;
	st.building.push_back( s );
	return true;
}

static void R_StreamCancelReads( streamImage_s *s ) {
	for ( int i = 0; i < STREAM_SLOTS; i++ ) {
		streamSlot_t &sl = st.slot[i];
		if ( sl.img != s ) {
			continue;
		}
		if ( sl.state == SLOT_READING ) {
			ri.FS_RangeCancel( sl.request );
		}
		if ( sl.state == SLOT_READING || sl.state == SLOT_READY ) {
			sl.state = SLOT_FREE;
			sl.img = NULL;
		}
		// a copied slot frees itself once its frame is done
	}
	s->slicesOut = 0;
}

static void R_StreamAbortBuild( streamImage_s *s ) {
	if ( s->buildLevel < 0 ) {
		return;
	}
	R_StreamCancelReads( s );
	R_StreamRetire( s->bHandle, s->bView, s->bSet, s->bMemory, s->bBytes );
	st.usedBytes -= s->bBytes;
	s->bHandle = VK_NULL_HANDLE;
	s->bView = VK_NULL_HANDLE;
	s->bSet = VK_NULL_HANDLE;
	s->bMemory = VK_NULL_HANDLE;
	s->bBytes = 0;
	s->buildLevel = -1;
	R_StreamBuildingRemove( s );
	R_StreamMark( s );
}

// back to the tail: the tail's set is rewritten first (r_textureMode may have changed samplers while it was not bound)
static void R_StreamDropToTail( streamImage_s *s ) {
	if ( s->residentLevel == s->tailLevel ) {
		return;
	}
	vk_write_image_descriptor( s->tailSet, s->tailView, s->image, qtrue );
	R_StreamRetire( s->handle, s->view, s->set, s->memory, s->bytes );
	st.usedBytes -= s->bytes;
	s->handle = VK_NULL_HANDLE;
	s->view = VK_NULL_HANDLE;
	s->set = VK_NULL_HANDLE;
	s->memory = VK_NULL_HANDLE;
	s->bytes = 0;
	s->residentLevel = s->tailLevel;
	s->image->handle = s->tailHandle;
	s->image->view = s->tailView;
	s->image->descriptor_set = s->tailSet;
	s->boundSerial = st.serial;
	st.evictions++;
	R_StreamMark( s );
}

// the build's first commands: all its levels to TRANSFER_DST, then the levels the bound image has, copied on the GPU
static void R_StreamStartBuild( VkCommandBuffer cmd, streamImage_s *s ) {
	const uint32_t levels = s->numLevels - s->buildLevel;
	R_StreamBarrier( cmd, s->bHandle, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
	const int from = MAX( s->buildLevel, s->residentLevel );
	VkImage src = s->image->handle;
	const uint32_t srcBase = from - s->residentLevel;
	const uint32_t count = s->numLevels - from;
	VkImageCopy regions[STREAM_MAX_LEVELS];
	for ( uint32_t i = 0; i < count; i++ ) {
		const int level = from + i;
		VkImageCopy &r = regions[i];
		Com_Memset( &r, 0, sizeof( r ) );
		r.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		r.srcSubresource.mipLevel = srcBase + i;
		r.srcSubresource.layerCount = MAX( s->layers, 1 );
		r.dstSubresource = r.srcSubresource;
		r.dstSubresource.mipLevel = level - s->buildLevel;
		r.extent.width = R_StreamLevelDim( s->width, level );
		r.extent.height = R_StreamLevelDim( s->height, level );
		r.extent.depth = 1;
	}
	R_StreamBarrier( cmd, src, srcBase, count, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
	qvkCmdCopyImage( cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->bHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions );
	R_StreamBarrier( cmd, src, srcBase, count, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT );
	s->bStarted = true;
}

// the build's last slice is copied: readable from this frame's draws on
static void R_StreamFinishBuild( VkCommandBuffer cmd, streamImage_s *s ) {
	R_StreamBarrier( cmd, s->bHandle, 0, s->numLevels - s->buildLevel, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT );
	if ( s->residentLevel != s->tailLevel ) {
		R_StreamRetire( s->handle, s->view, s->set, s->memory, s->bytes );
		st.usedBytes -= s->bytes;
	}
	s->residentLevel = s->buildLevel;
	s->handle = s->bHandle;
	s->view = s->bView;
	s->set = s->bSet;
	s->memory = s->bMemory;
	s->bytes = s->bBytes;
	s->image->handle = s->handle;
	s->image->view = s->view;
	s->image->descriptor_set = s->set;
	s->boundSerial = st.serial;
	ri.Printf( PRINT_DEVELOPER, "stream: %s %dx%d in %d ms\n", s->image->imgName, R_StreamLevelDim( s->width, s->buildLevel ),
		R_StreamLevelDim( s->height, s->buildLevel ), ri.Milliseconds() - s->buildStarted );
	s->bHandle = VK_NULL_HANDLE;
	s->bView = VK_NULL_HANDLE;
	s->bSet = VK_NULL_HANDLE;
	s->bMemory = VK_NULL_HANDLE;
	s->bBytes = 0;
	s->buildLevel = -1;
	R_StreamBuildingRemove( s );
	R_StreamMark( s );	// (a step down may want back up once there is room)
}

// the next slice of a build into a free slot; false when the build has read everything
static bool R_StreamIssueRead( streamImage_s *s, streamSlot_t &sl ) {
	const int stopLevel = s->residentLevel > s->buildLevel ? s->residentLevel : s->buildLevel;
	if ( s->nextLevel >= stopLevel ) {
		return false;
	}
	const int level = s->nextLevel;
	const int blockRows = ( R_StreamLevelDim( s->height, level ) + 3 ) / 4;
	const uint32_t rowBytes = ( ( R_StreamLevelDim( s->width, level ) + 3 ) / 4 ) * s->blockBytes;
	const int rows = MIN( blockRows - s->nextRow, (int)MAX( st.slotBytes / rowBytes, 1u ) );
	const uint32_t bytes = rows * rowBytes;
	if ( bytes > st.slotBytes ) {
		s->failed = true;	// one block row larger than a slot (wider than 64K texels): not streamed further
		return false;
	}
	const uint32_t offset = s->levelOffset[level] + s->nextRow * rowBytes;
	byte *dest = st.stagingPtr + (size_t)( &sl - st.slot ) * st.slotBytes;
	int request = ri.FS_RangeReadAsync( s->file, offset, bytes, dest );
	// a refused read is what every range handle gives after a file system restart without a renderer
	// restart (FS_Shutdown moves the handles' generation on); the texture would stay at its tail for the map. Reopen
	// the path once, and keep the new handle only for a file of the same length (the levels' offsets are its own)
	if ( !request && !s->reopened && s->source == SRC_RANGE && s->path[0] ) {
		s->reopened = true;
		int length = 0;
		const int file = ri.FS_RangeOpen( s->path, &length );
		if ( file && length == s->fileLength ) {
			ri.FS_RangeClose( s->file );
			s->file = file;
			request = ri.FS_RangeReadAsync( s->file, offset, bytes, dest );
			ri.Printf( PRINT_DEVELOPER, "stream: %s reopened%s\n", s->image->imgName, request ? "" : ", still refused" );
		} else if ( file ) {
			ri.FS_RangeClose( file );
		}
	}
	if ( !request ) {
		s->failed = true;
		return false;
	}
	sl.state = SLOT_READING;
	sl.img = s;
	sl.request = request;
	sl.level = level;
	sl.row = s->nextRow;
	sl.rows = rows;
	sl.bytes = bytes;
	s->slicesOut++;
	s->nextRow += rows;
	if ( s->nextRow >= blockRows ) {
		s->nextLevel++;
		s->nextRow = 0;
	}
	return true;
}

static void R_StreamCopySlice( VkCommandBuffer cmd, streamSlot_t &sl ) {
	streamImage_s *s = sl.img;
	VkBufferImageCopy r;
	Com_Memset( &r, 0, sizeof( r ) );
	r.bufferOffset = (VkDeviceSize)( &sl - st.slot ) * st.slotBytes;
	r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	r.imageSubresource.mipLevel = sl.level - s->buildLevel;
	r.imageSubresource.layerCount = 1;
	r.imageOffset.y = sl.row * 4;
	r.imageExtent.width = R_StreamLevelDim( s->width, sl.level );
	r.imageExtent.height = MIN( sl.rows * 4, R_StreamLevelDim( s->height, sl.level ) - sl.row * 4 );
	r.imageExtent.depth = 1;
	qvkCmdCopyBufferToImage( cmd, st.staging, s->bHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r );
	sl.state = SLOT_COPIED;
	sl.serial = st.serial;
	sl.img = NULL;
	s->slicesOut--;
	st.uploadedMB += sl.bytes / ( 1024.0 * 1024.0 );
}

static float R_StreamPriority( const streamImage_s *s, int want ) {
	const float have = (float)MAX( s->width >> s->residentLevel, 1 );
	return ( R_StreamPinned( s ) ? 1e6f : 0.0f ) + (float)( s->residentLevel - want ) + MAX( s->wantCur, s->wantPrev ) / have;
}

static void R_StreamListIdle( std::vector<streamImage_s *> &idle, bool &listed );

// make room for need bytes: images not wanted lately go back to their tails, least recently wanted first. idle is the
// frame's list of them (made on the first call that needs it, oldest first; *next: the first not yet dropped), so a
// full pool costs one scan a frame, not one per candidate level (a render scene streams 20,000 images)
static bool R_StreamMakeRoom( VkDeviceSize need, const streamImage_s *except, std::vector<streamImage_s *> &idle, bool &listed,
	size_t *next ) {
	if ( st.usedBytes + st.retiredBytes + need <= st.poolBytes ) {
		return true;
	}
	R_StreamListIdle( idle, listed );
	// (until the live images fit: a dropped image's bytes stay counted as retired for two frames, and counting them
	// here sent every idle image to its tail for one build)
	for ( ; *next < idle.size() && st.usedBytes + need > st.poolBytes; ( *next )++ ) {
		if ( idle[*next] != except && idle[*next]->residentLevel < idle[*next]->tailLevel ) {
			R_StreamDropToTail( idle[*next] );
		}
	}
	return st.usedBytes + st.retiredBytes + need <= st.poolBytes;
}

// a fragmented pool over its budget (R_StreamRelieve): the free ranges add up to the excess but no block empties, so
// R_StreamMakeRoom's fit test passes at once; idle images go to their tails, oldest first, until bytes amounting to the
// excess have left, before any drawn image is trimmed
static void R_StreamDropIdle( VkDeviceSize over, std::vector<streamImage_s *> &idle, bool &listed, size_t *next ) {
	R_StreamListIdle( idle, listed );
	const VkDeviceSize before = st.usedBytes;
	for ( ; *next < idle.size() && before - st.usedBytes < over; ( *next )++ ) {
		if ( idle[*next]->residentLevel < idle[*next]->tailLevel ) {
			R_StreamDropToTail( idle[*next] );
		}
	}
}

// the frame's idle images (not wanted lately, not building, not pinned, unbound for a frame cycle), oldest first: made
// once a frame, on the first call that needs it
static void R_StreamListIdle( std::vector<streamImage_s *> &idle, bool &listed ) {
	if ( !listed ) {
		const int now = ri.Milliseconds();
		for ( size_t i = 0; i < st.images.size(); i++ ) {
			streamImage_s *s = st.images[i];
			if ( s->residentLevel < s->tailLevel && s->buildLevel < 0 && !R_StreamPinned( s )
				&& R_StreamIdle( s, now ) && st.serial >= s->boundSerial + NUM_COMMAND_BUFFERS ) {
				idle.push_back( s );
			}
		}
		std::sort( idle.begin(), idle.end(), []( const streamImage_s *a, const streamImage_s *b ) { return a->wantedMs < b->wantedMs; } );
		listed = true;
	}
}

// the frame's builds: the candidates found in one pass, the most wanted first (a pass per build cost milliseconds once
// a render scene's 20,000 images stream)
static void R_StreamStartBuilds( int now ) {
	int active = (int)st.building.size();
	std::vector<std::pair<float, streamImage_s *> > cand;
	// the marked images (R_StreamMark): those still wanting sharper than they have stay marked (a retry pending, a tail
	// not ready, no room yet), the others leave the list
	for ( size_t i = 0; i < st.wanting.size(); ) {
		streamImage_s *s = st.wanting[i];
		bool keep = false;
		if ( !s->failed && s->buildLevel < 0 && s->source == SRC_RANGE ) {	// (whole-loaded: R_StreamReloadStep)
			const int want = R_StreamWantLevel( s );
			if ( want < s->residentLevel ) {
				keep = true;
				if ( s->tailSet && now >= s->retryMs ) {
					cand.push_back( std::make_pair( R_StreamPriority( s, want ), s ) );
				}
			}
		}
		if ( keep ) {
			i++;
		} else {
			s->inWanting = false;
			st.wanting[i] = st.wanting.back();
			st.wanting.pop_back();
		}
	}
	if ( active >= STREAM_BUILDS || cand.empty() ) {
		return;
	}
	const size_t top = MIN( cand.size(), (size_t)( 4 * STREAM_BUILDS ) );
	std::partial_sort( cand.begin(), cand.begin() + top, cand.end(),
		[]( const std::pair<float, streamImage_s *> &a, const std::pair<float, streamImage_s *> &b ) { return a.first > b.first; } );
	std::vector<streamImage_s *> idle;
	bool listed = false;
	size_t idleNext = 0;
	for ( size_t k = 0; k < top && active < STREAM_BUILDS; k++ ) {
		streamImage_s *best = cand[k].second;
		// the wanted level, else the sharpest that fits
		const int want = R_StreamWantLevel( best );
		int level = want;
		bool soon = false;
		while ( level < best->residentLevel ) {
			const VkDeviceSize need = R_StreamLevelsBytes( best, level ) + ( 1 << 16 );
			if ( R_StreamMakeRoom( need, best, idle, listed, &idleNext ) ) {
				break;
			}
			if ( st.usedBytes + need <= st.poolBytes ) {
				soon = true;	// room once the retired images are freed (two frames): looked at again then
				break;
			}
			level++;
		}
		if ( soon ) {
			continue;
		}
		if ( level > want ) {	// the pool is full of wanted images: R_StreamRelieve finds this much next frame
			st.starvedBytes = MAX( st.starvedBytes, R_StreamLevelsBytes( best, want ) + ( 1 << 16 ) );
		}
		if ( level >= best->residentLevel ) {
			best->retryMs = now + 1000;
			continue;
		}
		if ( !R_StreamCreateBuild( best, level ) ) {	// (out of memory may pass as the retired images are freed)
			best->failures++;
			ri.Printf( PRINT_WARNING, "stream: no image for %s at level %d%s\n", best->image->imgName, level, best->failures >= 4 ? "" : ", retried" );
			if ( best->failures >= 4 )
				best->failed = true;
			else
				best->retryMs = now + 5000 * best->failures;
			continue;
		}
		active++;
	}
}

/*
Over the pool (R_StreamRelieve): what the pool gives back, in the order of what the screen loses.

"Over" is real memory where the pool is what the budget leaves: the blocks, free ranges and all, against the pool.
Where r_streamPoolMB (or the 128 MB floor) sets the pool it is the images' own bytes.
*/

// the emptiest block's images rebuilt at the level they have in the other blocks' free ranges (a GPU copy, finished
// this frame), so the block empties and goes back to the driver. false: nothing moved
static bool R_StreamCompact( void ) {
	int emptiest = -1, blocks = 0;
	for ( size_t i = 0; i < st_blocks.size(); i++ ) {
		if ( st_blocks[i].memory && st_blocks[i].used ) {
			blocks++;
			if ( emptiest < 0 || st_blocks[i].used < st_blocks[emptiest].used ) {
				emptiest = (int)i;
			}
		}
	}
	if ( blocks < 2 ) {
		return false;
	}
	VkDeviceSize room = 0;
	for ( size_t i = 0; i < st_blocks.size(); i++ ) {
		if ( (int)i != emptiest && st_blocks[i].memory && st_blocks[i].type == st_blocks[emptiest].type ) {
			room += st_blocks[i].size - st_blocks[i].used;
		}
	}
	if ( room < st_blocks[emptiest].used ) {
		return false;
	}
	int active = (int)st.building.size();
	bool moved = false;
	for ( size_t i = 0; i < st.images.size() && active < STREAM_BUILDS; i++ ) {
		streamImage_s *s = st.images[i];
		if ( !s->handle || s->buildLevel >= 0 || s->failed || st.serial < s->boundSerial + NUM_COMMAND_BUFFERS ) {
			continue;
		}
		const std::unordered_map<VkImage, streamRange_t>::const_iterator r = st_ranges.find( s->handle );
		if ( r == st_ranges.end() || (int)r->second.block != emptiest ) {
			continue;
		}
		if ( R_StreamCreateBuild( s, s->residentLevel, emptiest ) ) {
			active++;
			moved = true;
			st.compactions++;
		}
	}
	return moved;
}

// levels the screen does not ask for: unpinned, drawn images sharper than asked (by two levels or more; by one once
// the squeeze is on) rebuilt at the level asked (one above it without the squeeze: the spare level a 2x approach needs)
// by a GPU copy, the largest savings first, until they cover cut. A whole-loaded texture nothing draws is left to
// R_StreamIdle's rule. false: nothing to trim
static bool R_StreamTrim( VkDeviceSize cut, int now ) {
	struct cand_t { streamImage_s *s; int to; VkDeviceSize saves; };
	std::vector<cand_t> cand;
	const int spare = st.squeeze ? 0 : 1;
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		streamImage_s *s = st.images[i];
		if ( s->buildLevel >= 0 || s->residentLevel >= s->tailLevel || R_StreamPinned( s ) || s->failed || !s->handle
			|| st.serial < s->boundSerial + NUM_COMMAND_BUFFERS ) {
			continue;
		}
		if ( s->source != SRC_RANGE && !st.squeeze && MAX( s->wantCur, s->wantPrev ) <= 0.0f ) {
			continue;
		}
		cand_t c;
		c.s = s;
		c.to = MIN( R_StreamWantLevel( s ) - spare, s->tailLevel );
		if ( c.to <= s->residentLevel ) {
			continue;
		}
		const VkDeviceSize after = c.to >= s->tailLevel ? 0 : R_StreamLevelsBytes( s, c.to );
		c.saves = s->bytes > after ? s->bytes - after : 0;
		cand.push_back( c );
	}
	if ( cand.empty() ) {
		return false;
	}
	std::sort( cand.begin(), cand.end(), []( const cand_t &a, const cand_t &b ) { return a.saves > b.saves; } );
	int active = (int)st.building.size();
	bool any = false;
	for ( size_t k = 0; k < cand.size() && cut > 0 && active < STREAM_BUILDS; k++ ) {
		streamImage_s *s = cand[k].s;
		if ( cand[k].to >= s->tailLevel ) {
			R_StreamDropToTail( s );
		} else if ( R_StreamCreateBuild( s, cand[k].to ) ) {
			active++;
		} else {
			continue;
		}
		s->retryMs = now + 2000;	// not straight back up
		cut -= MIN( cut, cand[k].saves );
		st.stepDowns++;
		st.trims++;
		any = true;
	}
	return any || active >= STREAM_BUILDS;	// (builds in the way: again next frame, not the squeeze)
}

// the squeeze one step down, when what that restores fits with a tenth of the pool to spare, not sooner than 5 s after
// its last change (looked at once a second)
static void R_StreamEase( int now ) {
	if ( !st.squeeze || ( now >= st.squeezeMs && now - st.squeezeMs < 5000 ) || ( now >= st.easeMs && now - st.easeMs < 1000 ) ) {
		return;
	}
	st.easeMs = now;
	VkDeviceSize need = 0;
	st.squeeze--;
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		const streamImage_s *s = st.images[i];
		const int level = R_StreamWantLevel( s );
		if ( level < s->residentLevel ) {
			need += R_StreamLevelsBytes( s, level ) - ( s->residentLevel < s->tailLevel ? R_StreamLevelsBytes( s, s->residentLevel ) : 0 );
		}
	}
	st.squeeze++;
	if ( st.usedBytes + st.retiredBytes + need > st.poolBytes - st.poolBytes / 10 ) {
		return;
	}
	st.squeeze--;
	st.squeezeMs = now;
	ri.Printf( PRINT_DEVELOPER, "stream: squeeze down to %d (%.0f MB to restore, %.0f of %.0f MB held)\n", st.squeeze,
		need / ( 1024.0 * 1024.0 ), st.usedBytes / ( 1024.0 * 1024.0 ), st.poolBytes / ( 1024.0 * 1024.0 ) );
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		R_StreamMark( st.images[i] );
	}
}

// r_streamIdleMB: what nothing draws any more is a cache for coming back, and it is capped: past the cap the least
// recently drawn go back to their tails (looked at once a second)
static void R_StreamIdleCap( int now ) {
	if ( now >= st.idleMs && now - st.idleMs < 1000 ) {
		return;
	}
	st.idleMs = now;
	std::vector<streamImage_s *> idle;
	VkDeviceSize bytes = 0;
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		streamImage_s *s = st.images[i];
		if ( s->residentLevel < s->tailLevel && s->buildLevel < 0 && !R_StreamPinned( s ) && R_StreamIdle( s, now ) ) {
			bytes += s->bytes;
			if ( st.serial >= s->boundSerial + NUM_COMMAND_BUFFERS ) {
				idle.push_back( s );
			}
		}
	}
	st.idleBytes = bytes;
	const VkDeviceSize cap = (VkDeviceSize)MAX( r_streamIdleMB->integer, 0 ) << 20;
	if ( !cap || bytes <= cap ) {
		return;
	}
	std::sort( idle.begin(), idle.end(), []( const streamImage_s *a, const streamImage_s *b ) { return a->wantedMs < b->wantedMs; } );
	for ( size_t i = 0; i < idle.size() && bytes > cap; i++ ) {
		bytes -= MIN( bytes, idle[i]->bytes );
		R_StreamDropToTail( idle[i] );
		st.idleDrops++;
	}
	st.idleBytes = bytes;
}

static void R_StreamRelieve( int now ) {
	const VkDeviceSize starved = st.starvedBytes;
	st.starvedBytes = 0;
	R_StreamIdleCap( now );
	std::vector<streamImage_s *> idle;
	bool listed = false;
	size_t next = 0;
	if ( st.usedBytes > st.poolBytes ) {	// the budget shrank under the pool: first what is not drawn
		R_StreamMakeRoom( 0, NULL, idle, listed, &next );
	}
	if ( !r_streamStepDown->integer ) {
		return;
	}
	// (retired images are as good as freed: two frames off)
	VkDeviceSize inUse;
	const VkDeviceSize blocks = R_StreamBlockBytes( &inUse );
	const VkDeviceSize real = st.poolCapped ? st.usedBytes : MAX( blocks > st.retiredBytes ? blocks - st.retiredBytes : 0, st.usedBytes );
	VkDeviceSize over = real > st.poolBytes ? real - st.poolBytes : 0;
	const VkDeviceSize slack = real - st.usedBytes;	// free ranges in the blocks
	bool starving = false;
	if ( !over ) {
		const VkDeviceSize room = st.poolBytes - real + slack;
		if ( starved <= room ) {
			R_StreamEase( now );
			return;
		}
		over = starved - room;	// a wanted build with no room: the others' surplus, then the squeeze, as if over by it
		starving = true;
	}
	VkDeviceSize cut = over;
	if ( !starving && slack ) {
		if ( slack >= over ) {	// the free ranges cover it: empty a block
			if ( now >= st.compactMs && now - st.compactMs < 500 ) {
				return;
			}
			st.compactMs = now;
			if ( R_StreamCompact() ) {
				return;
			}
			// (fragmented: more ranges have to go before a block empties)
			const VkDeviceSize before = st.usedBytes;
			R_StreamDropIdle( over, idle, listed, &next );
			if ( st.usedBytes < before ) {
				return;
			}
		} else {
			cut = over - slack;
		}
	}
	if ( R_StreamTrim( cut, now ) ) {
		return;
	}
	if ( st.squeeze < STREAM_SQUEEZE_MAX && ( now < st.squeezeMs || now - st.squeezeMs >= 250 ) ) {
		st.squeeze++;
		st.squeezeMs = now;
		ri.Printf( PRINT_DEVELOPER, "stream: squeeze up to %d (%s by %.0f MB; %.0f of %.0f MB held)\n", st.squeeze,
			starving ? "a wanted texture short" : "over", over / ( 1024.0 * 1024.0 ), st.usedBytes / ( 1024.0 * 1024.0 ),
			st.poolBytes / ( 1024.0 * 1024.0 ) );
	}
}

/*
Whole-loaded textures (r_streamWhole): the chain is in memory, not read by range. vk_upload_image_data copies it
through the renderer's staging buffer, whose commands vk_begin_frame submits ahead of the streaming commands and the
frame's draws; called from registration and from RE_BeginFrame, never inside vk_begin_frame.
*/
// data: the chain from level on (bytes long). An array: layerPixels, its layers' pixels (registration), or NULL to
// decode its files again
static bool R_StreamWholeUpload( streamImage_s *s, int level, byte *data, int bytes, byte **layerPixels = NULL ) {
	if ( !R_StreamCreateBuild( s, level ) ) {
		return false;
	}
	image_t *image = s->image;
	const VkImage bound = image->handle;
	image->handle = s->bHandle;	// (the upload names its target by the image)
	bool ok = true;
	if ( s->source == SRC_ARRAY ) {
		ok = R_ImageArrayUpload( image, s->files, s->layers, s->arrayNormals ? qtrue : qfalse, layerPixels, level ) ? true : false;
		bytes = (int)R_StreamLevelsBytes( s, level );
	} else {
		vk_upload_image_data( image, 0, 0, R_StreamLevelDim( s->width, level ), R_StreamLevelDim( s->height, level ),
			s->numLevels - level, data, bytes, qfalse );
	}
	image->handle = bound;
	if ( !ok ) {
		R_StreamAbortBuild( s );
		return false;
	}
	if ( s->residentLevel != s->tailLevel ) {
		R_StreamRetire( s->handle, s->view, s->set, s->memory, s->bytes );
		st.usedBytes -= s->bytes;
	}
	s->residentLevel = level;
	s->handle = s->bHandle;
	s->view = s->bView;
	s->set = s->bSet;
	s->memory = s->bMemory;
	s->bytes = s->bBytes;
	image->handle = s->handle;
	image->view = s->view;
	image->descriptor_set = s->set;
	s->boundSerial = st.serial;
	s->bHandle = VK_NULL_HANDLE;
	s->bView = VK_NULL_HANDLE;
	s->bSet = VK_NULL_HANDLE;
	s->bMemory = VK_NULL_HANDLE;
	s->bBytes = 0;
	s->buildLevel = -1;
	R_StreamBuildingRemove( s );
	st.uploadedMB += bytes / ( 1024.0 * 1024.0 );
	return true;
}

static uint32_t R_StreamChainOffset( const streamImage_s *s, int level ) {
	uint32_t n = 0;
	for ( int i = 0; i < level; i++ ) {
		n += s->levelBytes[i];
	}
	return n;
}

// registration, after R_StreamTailReady: the whole chain (level 0 first, bytes long) is in memory once, so it goes in
// whole while the pool has room or, the pool full, while the budget still has (r_vramReserveMB is there for loads):
// R_StreamRelieve then takes the levels the screen does not ask for back by GPU copies, which costs no second decode.
// Only with the device itself short does it go in smaller, or stay at its tail and come back by a reload
void R_StreamWholeLoaded( image_t *image, byte *levels, int bytes ) {
	streamImage_s *s = image->stream;
	if ( !s || s->source == SRC_RANGE || !R_StreamInitGpu() ) {
		return;
	}
	st.poolBytes = R_StreamPoolBytes();
	std::vector<streamImage_s *> idle;
	bool listed = false;
	size_t next = 0;
	int level = 0;
	while ( level < s->tailLevel && !R_StreamMakeRoom( R_StreamLevelsBytes( s, level ) + ( 1 << 16 ), s, idle, listed, &next )
		&& st.usedBytes + R_StreamLevelsBytes( s, level ) + ( 1 << 16 ) > st.poolBytes
		&& vk_vram_admit() < R_StreamLevelsBytes( s, level ) + ( 1 << 16 ) ) {
		level++;
	}
	const uint32_t at = R_StreamChainOffset( s, level );
	if ( level >= s->tailLevel || at >= (uint32_t)bytes ) {
		return;
	}
	if ( !R_StreamWholeUpload( s, level, levels + at, bytes - (int)at ) ) {
		ri.Printf( PRINT_DEVELOPER, "stream: no image for %s at level %d: at its tail\n", image->imgName, level );
	}
}

// registration of a layer array, after R_StreamTailReady: its layers' pixels are in memory once. It goes in at the level
// a close view could ask (a 4096-texel map's worth of screen through the layers' tiling: a 1024 array tiled 8 times is
// then 512), not whole: most arrays never need more, and one that does comes back sharper by a reload
void R_StreamArrayLoaded( image_t *image, byte **layerPixels ) {
	streamImage_s *s = image->stream;
	if ( !s || s->source != SRC_ARRAY || !R_StreamInitGpu() ) {
		return;
	}
	st.poolBytes = R_StreamPoolBytes();
	std::vector<streamImage_s *> idle;
	bool listed = false;
	size_t next = 0;
	int level = 0;
	const float want = 4096.0f * ( s->useScale > 0.0f ? s->useScale : 1.0f ), size = (float)MAX( s->width, s->height );
	while ( level < s->tailLevel && size / (float)( 1 << ( level + 1 ) ) >= want ) {
		level++;
	}
	while ( level < s->tailLevel && !R_StreamMakeRoom( R_StreamLevelsBytes( s, level ) + ( 1 << 16 ), s, idle, listed, &next )
		&& st.usedBytes + R_StreamLevelsBytes( s, level ) + ( 1 << 16 ) > st.poolBytes
		&& vk_vram_admit() < R_StreamLevelsBytes( s, level ) + ( 1 << 16 ) ) {
		level++;
	}
	if ( level < s->tailLevel ) {
		R_StreamWholeUpload( s, level, NULL, 0, layerPixels );
	}
}

// RE_BeginFrame: one whole-loaded texture the screen asks sharper than it is, read and decoded again as at
// registration and uploaded at the level asked (else the sharpest that fits)
void R_StreamReloadStep( void ) {
	if ( st.whole.empty() || !st.staging ) {
		return;
	}
	const int now = ri.Milliseconds();
	streamImage_s *best = NULL;
	float bestPriority = -1.0f;
	int bestWant = 0;
	for ( size_t i = 0; i < st.whole.size(); i++ ) {
		streamImage_s *s = st.whole[i];
		if ( s->failed || s->buildLevel >= 0 || !s->tailSet || now < s->retryMs || st.serial < s->boundSerial + NUM_COMMAND_BUFFERS ) {
			continue;
		}
		const int want = R_StreamWantLevel( s );
		if ( want >= s->residentLevel ) {
			continue;
		}
		const float p = R_StreamPriority( s, want );
		if ( p > bestPriority ) {
			best = s;
			bestPriority = p;
			bestWant = want;
		}
	}
	if ( !best ) {
		return;
	}
	streamImage_s *s = best;
	std::vector<streamImage_s *> idle;
	bool listed = false;
	size_t next = 0;
	int level = bestWant;
	while ( level < s->residentLevel ) {
		const VkDeviceSize need = R_StreamLevelsBytes( s, level ) + ( 1 << 16 );
		if ( R_StreamMakeRoom( need, s, idle, listed, &next ) ) {
			break;
		}
		if ( st.usedBytes + need <= st.poolBytes ) {
			return;	// room once the retired images are freed: next frame
		}
		level++;
	}
	if ( level > bestWant ) {
		st.starvedBytes = MAX( st.starvedBytes, R_StreamLevelsBytes( s, bestWant ) + ( 1 << 16 ) );
	}
	if ( level >= s->residentLevel ) {
		s->retryMs = now + 1000;
		return;
	}
	const double t0 = R_SunProfNow();
	byte *file = NULL, *levels = NULL;
	int bytes = 0;
	if ( s->source == SRC_ARRAY ) {
		if ( !R_StreamWholeUpload( s, level, NULL, 0, NULL ) ) {
			s->failures++;
			ri.Printf( PRINT_WARNING, "stream: reloading %s failed%s\n", s->image->imgName, s->failures >= 4 ? "" : ", retried" );
			if ( s->failures >= 4 ) {
				s->failed = true;
			} else {
				s->retryMs = now + 5000 * s->failures;
			}
			return;
		}
		const double ms = R_SunProfNow() - t0;
		st.reloads++;
		st.reloadMs += ms;
		ri.Printf( PRINT_DEVELOPER, "stream: %s %dx%d x%d reloaded in %.0f ms\n", s->image->imgName, R_StreamLevelDim( s->width, level ),
			R_StreamLevelDim( s->height, level ), s->layers, ms );
		return;
	}
	if ( s->source == SRC_DDS ) {
		const int len = ri.FS_ReadFile( s->path, (void **)&file );
		const uint32_t end = s->levelOffset[0] + R_StreamChainOffset( s, s->numLevels );
		if ( len > 0 && file && (uint32_t)len >= end ) {
			levels = file + s->levelOffset[0];
			bytes = (int)( end - s->levelOffset[0] );
		}
	} else {
		levels = R_StreamedRawLevels( s->image, s->path, &bytes );
	}
	const uint32_t at = R_StreamChainOffset( s, level );
	bool ok = levels && at < (uint32_t)bytes && (uint32_t)bytes >= R_StreamChainOffset( s, s->numLevels );
	if ( ok ) {
		ok = R_StreamWholeUpload( s, level, levels + at, bytes - (int)at );
	}
	if ( file ) {
		ri.FS_FreeFile( file );
	}
	if ( !ok ) {
		s->failures++;
		ri.Printf( PRINT_WARNING, "stream: reloading %s failed%s\n", s->image->imgName, s->failures >= 4 ? "" : ", retried" );
		if ( s->failures >= 4 ) {
			s->failed = true;
		} else {
			s->retryMs = now + 5000 * s->failures;
		}
		return;
	}
	const double ms = R_SunProfNow() - t0;
	st.reloads++;
	st.reloadMs += ms;
	ri.Printf( PRINT_DEVELOPER, "stream: %s %dx%d reloaded in %.0f ms\n", s->image->imgName, R_StreamLevelDim( s->width, level ),
		R_StreamLevelDim( s->height, level ), ms );
}

void R_StreamVkBeginFrame( void ) {
	if ( st.images.empty() || !R_StreamInitGpu() ) {
		return;
	}
	const double t0 = R_SunProfNow();
	const int now = ri.Milliseconds();
	st.serial++;
	st.poolBytes = R_StreamPoolBytes();
	R_StreamRelieve( now );	// over the pool: what it gives back; under it: the squeeze eased

	// what the frames in flight no longer use
	for ( size_t i = 0; i < st.retired.size(); ) {
		if ( st.serial >= st.retired[i].serial ) {
			R_StreamDestroy( st.retired[i].handle, st.retired[i].view, st.retired[i].set, st.retired[i].memory );
			st.retiredBytes -= MIN( st.retired[i].bytes, st.retiredBytes );
			st.retired[i] = st.retired.back();
			st.retired.pop_back();
		} else {
			i++;
		}
	}
	for ( int i = 0; i < STREAM_SLOTS; i++ ) {
		streamSlot_t &sl = st.slot[i];
		if ( sl.state == SLOT_COPIED && st.serial >= sl.serial + NUM_COMMAND_BUFFERS ) {
			sl.state = SLOT_FREE;
		} else if ( sl.state == SLOT_READING ) {
			const int r = ri.FS_RangeState( sl.request );
			if ( r > 0 ) {
				sl.state = SLOT_READY;
			} else if ( r < 0 ) {
				streamImage_s *s = sl.img;
				s->failures++;
				ri.Printf( PRINT_WARNING, "stream: reading %s failed%s\n", s->image->imgName, s->failures >= 4 ? "" : ", retried" );
				sl.state = SLOT_FREE;
				sl.img = NULL;
				s->slicesOut--;
				if ( s->failures >= 4 )
					s->failed = true;
				else
					s->retryMs = now + 5000 * s->failures;	// (a short read while the disk is busy)
				R_StreamAbortBuild( s );
			}
		}
	}

	if ( r_streamBias->integer != st.biasSeen ) {	// every image's wanted level moved
		st.biasSeen = r_streamBias->integer;
		for ( size_t i = 0; i < st.images.size(); i++ ) {
			R_StreamMark( st.images[i] );
		}
	}

	// want windows
	if ( now - st.windowMs >= STREAM_WINDOW_MS || now < st.windowMs ) {
		st.windowMs = now;
		for ( size_t i = 0; i < st.images.size(); i++ ) {
			st.images[i]->wantPrev = st.images[i]->wantCur;
			st.images[i]->wantCur = 0.0f;
		}
	}

	R_StreamStartBuilds( now );

	// arrived slices, oldest builds first, within the frame's copy budget (the first one always: a slice may be
	// larger than the budget)
	uint32_t budget = (uint32_t)MAX( r_streamUploadMB->integer, 1 ) << 20;
	bool copied = false;
	const std::vector<streamImage_s *> building = st.building;	// (a finish leaves st.building)
	for ( size_t i = 0; i < building.size(); i++ ) {
		streamImage_s *s = building[i];
		if ( s->buildLevel < 0 ) {
			continue;
		}
		if ( !s->bStarted ) {
			R_StreamStartBuild( R_StreamCmd(), s );
		}
		for ( int k = 0; k < STREAM_SLOTS && budget; k++ ) {
			streamSlot_t &sl = st.slot[k];
			if ( sl.state == SLOT_READY && sl.img == s && ( sl.bytes <= budget || !copied ) ) {
				budget -= MIN( sl.bytes, budget );
				copied = true;
				R_StreamCopySlice( R_StreamCmd(), sl );
			}
		}
		const int stopLevel = s->residentLevel > s->buildLevel ? s->residentLevel : s->buildLevel;
		if ( s->nextLevel >= stopLevel && s->slicesOut == 0 ) {
			R_StreamFinishBuild( R_StreamCmd(), s );
		}
	}

	R_StreamSubmit();

	// more reads: free slots to the builds by priority
	for ( int k = 0; k < STREAM_SLOTS; k++ ) {
		streamSlot_t &sl = st.slot[k];
		if ( sl.state != SLOT_FREE ) {
			continue;
		}
		streamImage_s *best = NULL;
		float bestPriority = -1.0f;
		for ( size_t i = 0; i < st.building.size(); i++ ) {
			streamImage_s *s = st.building[i];
			const int stopLevel = s->residentLevel > s->buildLevel ? s->residentLevel : s->buildLevel;
			if ( s->buildLevel < 0 || s->failed || s->nextLevel >= stopLevel ) {
				continue;
			}
			const float p = R_StreamPriority( s, s->buildLevel );
			if ( p > bestPriority ) {
				best = s;
				bestPriority = p;
			}
		}
		if ( !best || !R_StreamIssueRead( best, sl ) ) {
			if ( best && best->failed ) {
				R_StreamAbortBuild( best );
			}
			break;
		}
	}

	// the warmed model sharp: once, for the harness and the developer console (looked at every 250 ms: a walk over
	// every streamed image)
	static int pinCheckedMs;
	if ( st.pinGen && !st.pinReported && ( now - pinCheckedMs >= 250 || now < pinCheckedMs ) ) {
		pinCheckedMs = now;
		int pinned = 0, sharp = 0;
		VkDeviceSize bytes = 0;
		for ( size_t i = 0; i < st.images.size(); i++ ) {
			const streamImage_s *s = st.images[i];
			if ( R_StreamPinned( s ) ) {
				pinned++;
				if ( s->residentLevel == 0 || s->failed ) {
					sharp++;
				}
				bytes += s->bytes;
			}
		}
		if ( pinned && sharp == pinned ) {
			st.pinReported = true;
			ri.Printf( PRINT_DEVELOPER, "stream: pinned model sharp: %d images, %.0f MB, %d ms after its warm-up began\n", pinned,
				bytes / ( 1024.0 * 1024.0 ), now - st.pinStarted );
		}
	}

	tr_loadProf[LOADPROF_STREAM] += R_SunProfNow() - t0;
	tr_loadProfCount[LOADPROF_STREAM]++;
}

// vk_texture_mode (the device is idle, new samplers): the sets streaming owns, which the image loop does not see
void R_StreamSamplersChanged( void ) {
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		streamImage_s *s = st.images[i];
		if ( s->tailSet ) vk_write_image_descriptor( s->tailSet, s->tailView, s->image, qtrue );
		if ( s->set ) vk_write_image_descriptor( s->set, s->view, s->image, qtrue );
		if ( s->bSet ) vk_write_image_descriptor( s->bSet, s->bView, s->image, qtrue );
	}
}

// vk_delete_textures (the device is idle): back to the tails, everything else freed; the images go next
void R_StreamShutdown( void ) {
	for ( int i = 0; i < STREAM_SLOTS; i++ ) {
		if ( st.slot[i].state == SLOT_READING ) {
			ri.FS_RangeCancel( st.slot[i].request );
		}
		st.slot[i].state = SLOT_FREE;
		st.slot[i].img = NULL;
	}
	for ( size_t i = 0; i < st.retired.size(); i++ ) {
		R_StreamDestroy( st.retired[i].handle, st.retired[i].view, st.retired[i].set, st.retired[i].memory );
	}
	st.retired.clear();
	st.retiredBytes = 0;
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		streamImage_s *s = st.images[i];
		R_StreamDestroy( s->bHandle, s->bView, s->bSet, s->bMemory );
		R_StreamDestroy( s->handle, s->view, s->set, s->memory );
		if ( s->tailHandle ) {
			s->image->handle = s->tailHandle;
			s->image->view = s->tailView;
			s->image->descriptor_set = s->tailSet;
		}
		if ( s->file ) {
			ri.FS_RangeClose( s->file );
		}
		s->image->stream = NULL;
		delete s;
	}
	st.images.clear();
	st.whole.clear();
	st.building.clear();
	st.wanting.clear();
	st.squeeze = 0;
	st.starvedBytes = 0;
	st.shaderImages.clear();
	st.density.clear();
	st.usedBytes = 0;
	st.tailBytes = 0;
	st.pinGen = 0;
}

// vk_shutdown: the staging buffer and the descriptor pool
void R_StreamVkShutdown( void ) {
	R_StreamShutdown();
	if ( st.stagingPtr ) {
		qvkUnmapMemory( vk.device, st.stagingMemory );
	}
	if ( st.staging ) {
		VK_DESTROY_BUFFER( vk.device, st.staging );
	}
	if ( st.stagingMemory ) {
		VK_FREE_MEMORY( vk.device, st.stagingMemory );
	}
	if ( st.pool ) {
		qvkDestroyDescriptorPool( vk.device, st.pool, NULL );
	}
	for ( size_t i = 0; i < st_blocks.size(); i++ ) {	// (R_StreamShutdown released every built image's range)
		if ( st_blocks[i].memory ) {
			VK_FREE_MEMORY( vk.device, st_blocks[i].memory );
		}
	}
	st_blocks.clear();
	st_ranges.clear();
	if ( st.commandPool ) {
		qvkDestroyCommandPool( vk.device, st.commandPool, NULL );	// frees st.cmd
	}
	st.commandPool = VK_NULL_HANDLE;
	st.recording = false;
	st.staging = VK_NULL_HANDLE;
	st.stagingMemory = VK_NULL_HANDLE;
	st.stagingPtr = NULL;
	st.pool = VK_NULL_HANDLE;
}

void R_StreamInfo_f( void ) {
	int sharp = 0, building = 0, tails = 0, failed = 0, wholeSharp = 0;
	VkDeviceSize wholeBytes = 0;
	for ( size_t i = 0; i < st.images.size(); i++ ) {
		const streamImage_s *s = st.images[i];
		if ( s->buildLevel >= 0 ) building++;
		if ( s->failed ) failed++;
		if ( s->residentLevel < s->tailLevel ) sharp++; else tails++;
		if ( s->source != SRC_RANGE && s->residentLevel < s->tailLevel ) {
			wholeSharp++;
			wholeBytes += s->bytes;
		}
		if ( ri.Cmd_Argc() > 1 ) {
			ri.Printf( PRINT_ALL, "%5d %5d want %5d %s%s%s%s %s\n", R_StreamLevelDim( MAX( s->width, s->height ), s->residentLevel ),
				MAX( s->width, s->height ), R_StreamLevelDim( MAX( s->width, s->height ), R_StreamWantLevel( s ) ),
				s->buildLevel >= 0 ? "building " : "", R_StreamPinned( s ) ? "pinned " : "", s->failed ? "failed " : "",
				s->source == SRC_DDS ? "whole-dds " : s->source == SRC_RAW ? "whole-raw " : s->source == SRC_ARRAY ? "whole-array " : "", s->image->imgName );
		}
	}
	ri.Printf( PRINT_ALL, "%d streamed textures: %d above their tails, %d at them, %d building, %d failed\n",
		(int)st.images.size(), sharp, tails, building, failed );
	ri.Printf( PRINT_ALL, "tails %.1f MB; above them %.0f of %.0f MB; %.0f MB uploaded, %d builds, %d dropped to the tail, %d stepped down a level\n",
		st.tailBytes / ( 1024.0 * 1024.0 ), st.usedBytes / ( 1024.0 * 1024.0 ), st.poolBytes / ( 1024.0 * 1024.0 ),
		st.uploadedMB, st.builds, st.evictions, st.stepDowns );
	VkDeviceSize inUse;
	const VkDeviceSize blocks = R_StreamBlockBytes( &inUse );
	ri.Printf( PRINT_ALL, "pressure: squeeze %d, %d trimmed, %d moved to empty a block; blocks %.0f MB holding %.0f MB; pool %s\n",
		st.squeeze, st.trims, st.compactions, blocks / ( 1024.0 * 1024.0 ), inUse / ( 1024.0 * 1024.0 ),
		st.poolCapped ? "capped" : "from the budget" );
	ri.Printf( PRINT_ALL, "whole-loaded: %d textures, %d above their tails holding %.0f MB; %d reloads, %.0f ms\n",
		(int)st.whole.size(), wholeSharp, wholeBytes / ( 1024.0 * 1024.0 ), st.reloads, st.reloadMs );
	ri.Printf( PRINT_ALL, "not drawn: %.0f MB held (r_streamIdleMB %d), %d sent to their tails by the cap\n",
		st.idleBytes / ( 1024.0 * 1024.0 ), r_streamIdleMB->integer, st.idleDrops );
	vk_upload_info();	// the buffer uploads beside them (vk_upload.cpp)
	vk_pipeline_async_info();
}
