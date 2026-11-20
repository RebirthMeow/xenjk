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

// cm_chunk.cpp -- collision chunks (docs/levelport-collision.md): the manifest, the slots and their index, the
// residency worker and its pack reader, the spawn preload, the residency frame and the report. The lump loaders
// and CM_ParseChunk stay in cm_load.cpp; the traces in cm_trace.cpp and cm_test.cpp.

#include "cm_local.h"
#include "cm_chunk.h"
#include "cm_zbsp.h"
#ifdef USE_INTERNAL_ZLIB
#include "zlib/zlib.h"
#else
#include <zlib.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include "qcommon/qfiles.h"

#define	VIS_HEADER	8	// the visibility lump's header, as cm_load.cpp's CMod_LoadVisibility reads it

cmChunk_t	*cm_chunks[MAX_CM_CHUNKS];		// collision chunks (cm_local.h), on TAG_BSP: their own blocks
int			cm_numChunks;

/*
==================
CM_FreeChunk

A chunk's blocks and its header: malloc's (CM_ParseChunk is the only chunk parser)
==================
*/
void CM_FreeChunk( cmChunk_t *c )
{
	for ( int i = 0 ; i < c->numAllocs ; i++ ) {
		free( c->allocs[i] );
	}
	free( c );
}

// the manifest (maps/<map>.chunks): every chunk of the map, resident or not, by slot
typedef struct {
	char	name[MAX_QPATH];
	vec3_t	origin;
	vec3_t	mins, maxs;			// placed bounds, read from the manifest: residency decides before loading
} cmManifestEntry_t;
static cmManifestEntry_t	*cm_manifest;
static int					cm_manifestCount;

#define MAX_CM_DRIVES	16
static vec3_t	cm_drives[MAX_CM_DRIVES];		// cm_drive's virtual drivers (the residency test's players)
static vec3_t	cm_drivesPrev[MAX_CM_DRIVES];	// where they were at the last residency frame (their velocity)
static int		cm_drivesPrevTime;
static int		cm_numDrives;

static struct {
	int			loads, evictions, violations, frames;
	int			heldFrames, firstHeld, lastHeld;	// virtual drivers standing in a chunk not swapped in
	int			clientHeldFrames;					// held clients, frames summed over them (CM_NoteClientHeld)
	int			resident, peakResident;
	long long	bytes, peakBytes;					// 64-bit: tag 2's walk alone peaked at 2.13 GB of blocks
	int			worstFrameMs, worstFrameLoads;		// residency's own work in a frame
	int			worstServerFrameMs;				// the whole server frame (CM_NoteServerFrame)
	int			maxReadMs, sumReadMs, maxParseMs, sumParseMs, failed, dead;	// the worker's; chunks given up
	int			maxDecodeMs, sumDecodeMs;			// the worker's XBSZ decodes (collision option C)
	int			preloads, preloadMs;				// loaded in place for a spawn or a start (cm_chunkpreload)
	int			spawnHolds, maxSpawnHoldMs;		// clients waiting at a spawn for the chunks around them
	int			falseWallFrames;					// a chunk within the load distance neither resident nor loading
	int			nearPendingFrames;					// a loading chunk's box within cm_chunkNearDist of a driver
	float		nearestUnloaded, nearestPending;	// the nearest a non-resident (a loading) chunk's bounds came
	int			capEvictions, capMisses;			// cm_chunkMegs
	int			cancelled;							// queued jobs no driver wanted any more, dropped before their read
	int			lastSpawnHoldMs, lastSpawnWaited, lastSpawnLoads;	// the last spawn wait: chunks missing, loads landed
	int			spawnedMissing;						// releases from a spawn hold with a chunk of the spawn's set missing
	int			spawnedBesideDead;					// releases with a chunk of the spawn's set given up (a box for good)
	int			preloadQueued, preloadCapSkips;		// the spawn preload: chunks given to the worker, left out by the cap
	long long	peakTransient;						// read buffers, parsed chunks not swapped in and chunks not yet freed
	int			spawnHoldMs[256];					// the first 256 waits, for the median
} cm_res;
int			cm_pendingStops;		// traces stopped by, or started in, a loading chunk's box (cm_trace; the false wall)
int			cm_pendingStopsHeld;	// those while a client was held (its own frozen view on the box it waits for)
int			cm_pendingStopsShort;	// those no longer than 256 u with no client held: a move against a loading box
qboolean	cm_heldNow;				// a client held this server frame (CM_NoteClientHeld; cleared by residency)

/*
===============================================================================

COLLISION CHUNK WORKER (docs/levelport-collision.md sections 19 to 24)

One worker thread reads a chunk's BSP from its pack with a reader of its own (stdio and zlib, no file-system or zone
state) and parses it into malloc'd blocks; the frame only swaps a finished chunk into its slot. Nothing here calls
Com_Error, Com_Printf, Cvar or the zone off the main thread: a bad chunk comes back as a failed job. The main thread
parses with the same CM_ParseChunk, from a buffer the file system read.

===============================================================================
*/

#ifdef _WIN32
#define CM_FSEEK64( f, o, w )	_fseeki64( f, o, w )
#define CM_FTELL64( f )			_ftelli64( f )
#else
#define CM_FSEEK64( f, o, w )	fseeko( f, (off_t)( o ), w )
#define CM_FTELL64( f )			( (long long)ftello( f ) )
#endif

typedef struct cmJob_s {
	int			slot, generation;
	char		name[MAX_QPATH];
	char		pak[MAX_OSPATH];
	vec3_t		origin;
	float		dist;					// the nearest driver, for the queue's order (updated by the main thread)
	cmChunk_t	*result;
	int			readMs, parseMs;
	int			decodeMs;				// an XBSZ chunk's decode (collision option C), apart from its read; 0 for a BSP
	bool		preload;				// the spawn preload's: swapped in with no driver (CM_SwapPreloadJobs)
	char		err[128];
} cmJob_t;

static std::mutex				cm_jobLock;
static std::condition_variable	cm_jobCv;
static std::vector<cmJob_t *>	cm_jobQueue, cm_jobDone;
static std::vector<cmChunk_t *>	cm_freeQueue;			// chunks out of their slots, for the worker to free
static std::vector<cmJob_t *>	cm_jobStaged;			// this frame's jobs, queued together (CM_FlushChunkJobs)
static int						cm_jobGeneration;		// a map change abandons every job of the one before
static std::atomic<int>			cm_jobTestDelayMs( 0 );
// bytes outside the slots: the worker's read buffer, parsed chunks not taken by the frame yet, chunks out of their slots
// not freed yet; counted with the resident bytes against cm_chunkMegs
static std::atomic<long long>	cm_transientBytes( 0 );
static std::atomic<bool>		cm_workerStop( false );
static std::thread				*cm_worker;
static byte						cm_slotPending[MAX_CM_CHUNKS];	// queued or being read (main thread's view)
static byte						cm_pinned[MAX_CM_CHUNKS];		// around a spawn: never evicted (cm_chunkPinSpawns)
static int						cm_keepFrames[MAX_CM_CHUNKS];	// under a static item: not evicted for these frames
static byte						cm_slotDead[MAX_CM_CHUNKS];		// failed on the worker and in place: given up
static int						cm_slotRetryAt[MAX_CM_CHUNKS];	// a transient failure's (out of memory) next try, ms
static bool						cm_lastFailTransient;			// CM_LoadChunkSlot's last failure: out of memory
static int						cm_idleSince;					// when residency last saw no driver (0: drivers)

// a chunk's load line: the console's on a server (the gates read it there), developer-only on a remote client, whose
// residency runs in its frame: every swap was a line on the play notify area
typedef void ( QDECL *cmPrint_t )( const char *fmt, ... );
static cmPrint_t CM_LoadPrint( void )
{
	return com_sv_running && com_sv_running->integer ? Com_Printf : Com_DPrintf;
}
static int						cm_lastWanted[MAX_CM_CHUNKS];	// the frame a driver last had it within reach (the LRU)
static int						cm_frameSeq;					// residency frames since the process started
int								cm_numUnloaded;
vec3_t							cm_unloadedMins[MAX_CM_CHUNKS], cm_unloadedMaxs[MAX_CM_CHUNKS];	// by slot
byte							cm_slotSolid[MAX_CM_CHUNKS];

// the chunk index (section 25): the manifest's slots by 2D cell of CM_GRID_CELL u, CSR arrays
#define CM_GRID_CELL	4800.0f
static struct {
	float	x0, y0;					// the grid's min corner
	int		nx, ny;
	int		*cellStart;				// nx * ny + 1 offsets into slots
	int		*slots;
} cm_grid;
static byte		cm_slotAlways[MAX_CM_CHUNKS];	// visited by every query: wider than its manifest bounds
static int		cm_candStamp[MAX_CM_CHUNKS], cm_candSeq;
static void		CM_FreeChunkGrid( void );

// a zip entry name against ours, as FS_FilenameCompare compares: no case, either slash
static qboolean CM_ZipNameIs( const byte *zipName, int zipLen, const char *name )
{
	for ( int i = 0 ; i < zipLen ; i++ ) {
		int a = zipName[i], b = (byte)name[i];
		if ( !b ) {
			return qfalse;
		}
		a = a == '\\' ? '/' : tolower( a );
		b = b == '\\' ? '/' : tolower( b );
		if ( a != b ) {
			return qfalse;
		}
	}
	return name[zipLen] ? qfalse : qtrue;
}

static unsigned long long CM_ZipU64( const byte *v )
{
	unsigned long long q = 0;
	for ( int b = 7 ; b >= 0 ; b-- ) {
		q = ( q << 8 ) | v[b];
	}
	return q;
}

// a zip entry by name, the pack read with stdio and inflated with zlib's own allocator, its CRC checked. NULL and err
// on failure. A zip64 entry (an entry past 2 GB of a pack) is read through its extra field, a zip64 directory (a pack
// past 4 GB or with 65,535 entries or more) through the zip64 end-of-central-directory record
static byte *CM_ZipRead( const char *zipPath, const char *name, int *outLen, char *err, int errLen )
{
	FILE		*f = fopen( zipPath, "rb" );
	byte		*cd = NULL, *comp = NULL, *out = NULL;
	byte		tail[65557];
	long long	size;
	int			n, i;

	if ( !f ) {
		snprintf( err, errLen, "cannot open %s", zipPath );
		return NULL;
	}
	CM_FSEEK64( f, 0, SEEK_END );
	size = CM_FTELL64( f );
	n = (int)( size < (long long)sizeof( tail ) ? size : (long long)sizeof( tail ) );
	CM_FSEEK64( f, size - n, SEEK_SET );
	if ( n < 22 || (int)fread( tail, 1, n, f ) != n ) {
		snprintf( err, errLen, "short read of %s", zipPath );
		fclose( f );
		return NULL;
	}
	for ( i = n - 22 ; i >= 0 ; i-- ) {				// the end-of-central-directory record
		if ( tail[i] == 0x50 && tail[i + 1] == 0x4b && tail[i + 2] == 0x05 && tail[i + 3] == 0x06 ) {
			break;
		}
	}
	if ( i < 0 ) {
		snprintf( err, errLen, "%s: no zip directory", zipPath );
		fclose( f );
		return NULL;
	}
	unsigned long long entries = tail[i + 10] | ( tail[i + 11] << 8 );
	unsigned long long cdSize = tail[i + 12] | ( tail[i + 13] << 8 ) | ( tail[i + 14] << 16 ) | ( (unsigned)tail[i + 15] << 24 );
	unsigned long long cdOfs = tail[i + 16] | ( tail[i + 17] << 8 ) | ( tail[i + 18] << 16 ) | ( (unsigned)tail[i + 19] << 24 );
	if ( cdOfs == 0xffffffffu || cdSize == 0xffffffffu || entries == 0xffffu ) {
		// zip64: the locator just before the record gives the zip64 record's offset, which holds the true counts
		const byte	*loc = i >= 20 ? tail + i - 20 : NULL;
		byte		z64[56];
		if ( !loc || !( loc[0] == 0x50 && loc[1] == 0x4b && loc[2] == 0x06 && loc[3] == 0x07 ) ) {
			snprintf( err, errLen, "%s: zip64 directory without its locator", zipPath );
			fclose( f );
			return NULL;
		}
		const unsigned long long z64Ofs = CM_ZipU64( loc + 8 );
		if ( z64Ofs + sizeof( z64 ) > (unsigned long long)size || CM_FSEEK64( f, (long long)z64Ofs, SEEK_SET ) ||
			 fread( z64, 1, sizeof( z64 ), f ) != sizeof( z64 ) ||
			 !( z64[0] == 0x50 && z64[1] == 0x4b && z64[2] == 0x06 && z64[3] == 0x06 ) ) {
			snprintf( err, errLen, "%s: bad zip64 directory record", zipPath );
			fclose( f );
			return NULL;
		}
		entries = CM_ZipU64( z64 + 32 );
		cdSize = CM_ZipU64( z64 + 40 );
		cdOfs = CM_ZipU64( z64 + 48 );
	}
	if ( cdSize > 0x7fffffffu || cdOfs + cdSize > (unsigned long long)size ) {
		snprintf( err, errLen, "%s: directory out of the file", zipPath );
		fclose( f );
		return NULL;
	}
	cd = (byte *)malloc( cdSize ? (size_t)cdSize : 1 );
	CM_FSEEK64( f, (long long)cdOfs, SEEK_SET );
	if ( !cd || fread( cd, 1, (size_t)cdSize, f ) != cdSize ) {
		snprintf( err, errLen, "%s: short directory", zipPath );
		goto fail;
	}
	{
		unsigned int p = 0;
		for ( unsigned long long e = 0 ; e < entries && p + 46 <= cdSize ; e++ ) {
			const byte *h = cd + p;
			if ( !( h[0] == 0x50 && h[1] == 0x4b && h[2] == 0x01 && h[3] == 0x02 ) ) {
				break;
			}
			const int method = h[10] | ( h[11] << 8 );
			const unsigned int crc = h[16] | ( h[17] << 8 ) | ( h[18] << 16 ) | ( (unsigned)h[19] << 24 );
			unsigned long long compSize = h[20] | ( h[21] << 8 ) | ( h[22] << 16 ) | ( (unsigned)h[23] << 24 );
			unsigned long long rawSize = h[24] | ( h[25] << 8 ) | ( h[26] << 16 ) | ( (unsigned)h[27] << 24 );
			const int fnLen = h[28] | ( h[29] << 8 ), exLen = h[30] | ( h[31] << 8 ), cmLen = h[32] | ( h[33] << 8 );
			unsigned long long local = h[42] | ( h[43] << 8 ) | ( h[44] << 16 ) | ( (unsigned)h[45] << 24 );
			if ( (unsigned long long)p + 46 + fnLen + exLen + cmLen > cdSize ) {
				break;										// the entry's names run past the directory
			}
			if ( CM_ZipNameIs( h + 46, fnLen, name ) ) {
				byte lh[30];
				// a saturated field is in the zip64 extra block (0x0001), in the order raw size, compressed size,
				// local header offset: Python's zipfile writes it for every entry past 2 GB of a pack (tag 3's 2.5 GB
				// pack sent its last chunks to the frame's load in place, a 387 ms frame)
				const bool z64 = compSize == 0xffffffffu || rawSize == 0xffffffffu || local == 0xffffffffu;
				if ( z64 ) {
					const byte *x = h + 46 + fnLen, *xe = x + exLen;
					bool found = false;
					while ( x + 4 <= xe ) {
						const int tag = x[0] | ( x[1] << 8 ), sz = x[2] | ( x[3] << 8 );
						if ( x + 4 + sz > xe ) {
							break;
						}
						if ( tag == 0x0001 ) {
							const byte *v = x + 4, *ve = v + sz;
							unsigned long long *fields[3] = { &rawSize, &compSize, &local };
							bool ok = true;
							for ( int k = 0 ; k < 3 ; k++ ) {
								if ( *fields[k] != 0xffffffffu ) {
									continue;
								}
								if ( v + 8 > ve ) {
									ok = false;
									break;
								}
								unsigned long long q = 0;
								for ( int b = 7 ; b >= 0 ; b-- ) {
									q = ( q << 8 ) | v[b];
								}
								*fields[k] = q;
								v += 8;
							}
							found = ok;
							break;
						}
						x += 4 + sz;
					}
					if ( !found ) {
						snprintf( err, errLen, "%s: %s: bad zip64 extra field", zipPath, name );
						goto fail;
					}
				}
				if ( rawSize > 0x7fffffffu || compSize > 0x7fffffffu || (long long)local + 30 > size ) {
					snprintf( err, errLen, "%s: %s is too large or out of the file", zipPath, name );
					goto fail;
				}
				CM_FSEEK64( f, local, SEEK_SET );
				if ( fread( lh, 1, 30, f ) != 30 || !( lh[0] == 0x50 && lh[1] == 0x4b && lh[2] == 0x03 && lh[3] == 0x04 ) ) {
					snprintf( err, errLen, "%s: bad local header for %s", zipPath, name );
					goto fail;
				}
				CM_FSEEK64( f, (long long)local + 30 + ( lh[26] | ( lh[27] << 8 ) ) + ( lh[28] | ( lh[29] << 8 ) ), SEEK_SET );
				comp = (byte *)malloc( compSize ? compSize : 1 );
				out = (byte *)malloc( rawSize ? rawSize : 1 );
				if ( !comp || !out ) {
					snprintf( err, errLen, "%s: out of memory for %s", zipPath, name );
					goto fail;
				}
				if ( fread( comp, 1, compSize, f ) != compSize ) {
					snprintf( err, errLen, "%s: short read of %s", zipPath, name );
					goto fail;
				}
				if ( method == 0 && compSize == rawSize ) {
					memcpy( out, comp, rawSize );
				} else if ( method == 8 ) {
					z_stream zs;
					memset( &zs, 0, sizeof( zs ) );		// zalloc NULL: zlib's own malloc, safe off the main thread
					if ( inflateInit2( &zs, -MAX_WBITS ) != Z_OK ) {
						snprintf( err, errLen, "inflateInit failed" );
						goto fail;
					}
					zs.next_in = comp;
					zs.avail_in = compSize;
					zs.next_out = out;
					zs.avail_out = rawSize;
					const int r = inflate( &zs, Z_FINISH );
					inflateEnd( &zs );
					if ( r != Z_STREAM_END || zs.total_out != rawSize ) {
						snprintf( err, errLen, "%s: inflate of %s failed (%d)", zipPath, name, r );
						goto fail;
					}
				} else {
					snprintf( err, errLen, "%s: %s has compression method %d", zipPath, name, method );
					goto fail;
				}
				if ( crc32( crc32( 0L, Z_NULL, 0 ), out, rawSize ) != crc ) {
					snprintf( err, errLen, "%s: %s fails its CRC", zipPath, name );
					goto fail;
				}
				free( comp );
				free( cd );
				fclose( f );
				*outLen = (int)rawSize;
				return out;
			}
			p += 46 + fnLen + exLen + cmLen;
		}
	}
	snprintf( err, errLen, "%s not in %s", name, zipPath );
fail:
	free( out );
	free( comp );
	free( cd );
	fclose( f );
	return NULL;
}

/*
==================
CM_ValidateChunk

Every check the lump loaders would Com_Error on, and every input they or the traces would read out of range, made
before they run, so a parse cannot fail in them: one model, no surfaces (patches allocate on the hunk), brushes of six
sides or more (the loader reads the bounds from the first six) and 65,535 at most, 65,534 shaders at most (16-bit
records), at most 2^27 planes less the box's twelve (a side's 27 bits of plane), a visibility lump empty or with its header, clusters in range, a node tree without
cycles
==================
*/
qboolean CM_ValidateChunk( const byte *buf, int len, char *err, int errLen )
{
	if ( len < (int)sizeof( dheader_t ) ) {
		snprintf( err, errLen, "too short for a BSP" );
		return qfalse;
	}
	const dheader_t *h = (const dheader_t *)buf;
	if ( LittleLong( h->version ) != BSP_VERSION ) {
		snprintf( err, errLen, "wrong version %d", LittleLong( h->version ) );
		return qfalse;
	}
	for ( int i = 0 ; i < HEADER_LUMPS ; i++ ) {
		const int ofs = LittleLong( h->lumps[i].fileofs ), n = LittleLong( h->lumps[i].filelen );
		if ( ofs < 0 || n < 0 || (long long)ofs + n > len ) {
			snprintf( err, errLen, "lump %d out of the file", i );
			return qfalse;
		}
	}
#define LUMP_COUNT( lump, type, min ) \
	const int lump##_n = LittleLong( h->lumps[lump].filelen ) / (int)sizeof( type ); \
	if ( LittleLong( h->lumps[lump].filelen ) % (int)sizeof( type ) || lump##_n < ( min ) ) { \
		snprintf( err, errLen, "lump " #lump ": bad size" ); \
		return qfalse; \
	} \
	const type *lump##_p = (const type *)( buf + LittleLong( h->lumps[lump].fileofs ) )
	LUMP_COUNT( LUMP_SHADERS, dshader_t, 1 );
	LUMP_COUNT( LUMP_MODELS, dmodel_t, 1 );
	LUMP_COUNT( LUMP_NODES, dnode_t, 1 );
	LUMP_COUNT( LUMP_LEAFS, dleaf_t, 1 );
	LUMP_COUNT( LUMP_PLANES, dplane_t, 1 );
	LUMP_COUNT( LUMP_LEAFBRUSHES, int, 0 );
	LUMP_COUNT( LUMP_LEAFSURFACES, int, 0 );
	LUMP_COUNT( LUMP_BRUSHSIDES, dbrushside_t, 0 );
	LUMP_COUNT( LUMP_BRUSHES, dbrush_t, 0 );
	LUMP_COUNT( LUMP_SURFACES, dsurface_t, 0 );
#undef LUMP_COUNT
	if ( LUMP_MODELS_n > 1 || LUMP_SURFACES_n || LUMP_LEAFSURFACES_n ) {
		snprintf( err, errLen, "a chunk has one model and no surfaces (%d, %d, %d)", LUMP_MODELS_n, LUMP_SURFACES_n,
			LUMP_LEAFSURFACES_n );
		return qfalse;
	}
	const int visLen = LittleLong( h->lumps[LUMP_VISIBILITY].filelen );
	if ( visLen && visLen < VIS_HEADER ) {
		snprintf( err, errLen, "visibility lump shorter than its header" );
		return qfalse;
	}
	for ( int i = 0 ; i < LUMP_NODES_n ; i++ ) {
		const dnode_t *nd = &LUMP_NODES_p[i];
		if ( (unsigned)LittleLong( nd->planeNum ) >= (unsigned)LUMP_PLANES_n ) {
			snprintf( err, errLen, "node %d: bad plane", i );
			return qfalse;
		}
		for ( int k = 0 ; k < 2 ; k++ ) {
			const int c = LittleLong( nd->children[k] );
			if ( c >= LUMP_NODES_n || ( c < 0 && -1 - c >= LUMP_LEAFS_n ) ) {
				snprintf( err, errLen, "node %d: bad child", i );
				return qfalse;
			}
		}
	}
	{	// a tree: every node reached from the root at most once (no cycle, no shared subtree)
		std::vector<byte> seen( LUMP_NODES_n, 0 );
		std::vector<int> stack;
		stack.push_back( 0 );
		while ( !stack.empty() ) {
			const int i = stack.back();
			stack.pop_back();
			if ( seen[i] ) {
				snprintf( err, errLen, "node %d reached twice: not a tree", i );
				return qfalse;
			}
			seen[i] = 1;
			for ( int k = 0 ; k < 2 ; k++ ) {
				const int c = LittleLong( LUMP_NODES_p[i].children[k] );
				if ( c >= 0 ) {
					stack.push_back( c );
				}
			}
		}
	}
	for ( int i = 0 ; i < LUMP_LEAFS_n ; i++ ) {
		const dleaf_t *lf = &LUMP_LEAFS_p[i];
		const long long fb = LittleLong( lf->firstLeafBrush ), nb = LittleLong( lf->numLeafBrushes );
		const long long fs = LittleLong( lf->firstLeafSurface ), ns = LittleLong( lf->numLeafSurfaces );
		const int cluster = LittleLong( lf->cluster ), area = LittleLong( lf->area );
		if ( fb < 0 || nb < 0 || fb + nb > LUMP_LEAFBRUSHES_n || fs < 0 || ns < 0 || fs + ns > LUMP_LEAFSURFACES_n ||
			 area >= MAX_MAP_AREAS || cluster < -1 || cluster > LUMP_LEAFS_n ) {
			snprintf( err, errLen, "leaf %d: bad ranges", i );
			return qfalse;
		}
	}
	for ( int i = 0 ; i < LUMP_LEAFBRUSHES_n ; i++ ) {
		if ( (unsigned)LittleLong( LUMP_LEAFBRUSHES_p[i] ) >= (unsigned)LUMP_BRUSHES_n ) {
			snprintf( err, errLen, "leaf brush %d: bad brush", i );
			return qfalse;
		}
	}
	if ( LUMP_SHADERS_n > 65534 ) {
		snprintf( err, errLen, "%d shaders (65,534 at most)", LUMP_SHADERS_n );
		return qfalse;
	}
	if ( LUMP_PLANES_n > CM_SIDE_PLANE + 1 - 12 ) {		// a side holds 27 bits of plane (and the box's twelve)
		snprintf( err, errLen, "%d planes (too many for a side's 27 bits)", LUMP_PLANES_n );
		return qfalse;
	}
	for ( int i = 0 ; i < LUMP_BRUSHSIDES_n ; i++ ) {
		if ( (unsigned)LittleLong( LUMP_BRUSHSIDES_p[i].planeNum ) >= (unsigned)LUMP_PLANES_n ||
			 (unsigned)LittleLong( LUMP_BRUSHSIDES_p[i].shaderNum ) >= (unsigned)LUMP_SHADERS_n ) {
			snprintf( err, errLen, "brush side %d: bad plane or shader", i );
			return qfalse;
		}
	}
	for ( int i = 0 ; i < LUMP_BRUSHES_n ; i++ ) {
		const long long fs = LittleLong( LUMP_BRUSHES_p[i].firstSide ), ns = LittleLong( LUMP_BRUSHES_p[i].numSides );
		if ( fs < 0 || ns < 6 || ns > 65535 || fs + ns > LUMP_BRUSHSIDES_n ||
			 (unsigned)LittleLong( LUMP_BRUSHES_p[i].shaderNum ) >= (unsigned)LUMP_SHADERS_n ) {
			snprintf( err, errLen, "brush %d: bad sides or shader", i );
			return qfalse;
		}
	}
	const dmodel_t *m = LUMP_MODELS_p;
	if ( LittleLong( m->firstBrush ) < 0 || LittleLong( m->numBrushes ) < 0 ||
		 (long long)LittleLong( m->firstBrush ) + LittleLong( m->numBrushes ) > LUMP_BRUSHES_n ) {
		snprintf( err, errLen, "model 0: bad brushes" );
		return qfalse;
	}
	return qtrue;
}

static void CM_ChunkWorker( void )
{
	for ( ;; ) {
		cmJob_t *job;
		{
			std::unique_lock<std::mutex> lock( cm_jobLock );
			cm_jobCv.wait( lock, [] { return cm_workerStop || !cm_jobQueue.empty() || !cm_freeQueue.empty(); } );
			if ( !cm_freeQueue.empty() ) {
				std::vector<cmChunk_t *> retired;
				retired.swap( cm_freeQueue );
				lock.unlock();
				for ( cmChunk_t *c : retired ) {
					const long long bytes = c->bytes;
					CM_FreeChunk( c );		// malloc'd blocks: freed here, off the server frame
					cm_transientBytes -= bytes;
				}
				continue;
			}
			if ( cm_workerStop ) {
				return;
			}
			size_t best = 0;					// the nearest driver's chunk first
			for ( size_t i = 1 ; i < cm_jobQueue.size() ; i++ ) {
				if ( cm_jobQueue[i]->dist < cm_jobQueue[best]->dist ) {
					best = i;
				}
			}
			job = cm_jobQueue[best];
			cm_jobQueue.erase( cm_jobQueue.begin() + best );
		}
		const auto t0 = std::chrono::steady_clock::now();
		int len = 0;
		byte *buf = CM_ZipRead( job->pak, job->name, &len, job->err, sizeof( job->err ) );
		const auto t1 = std::chrono::steady_clock::now();
		if ( buf ) {
			cm_transientBytes += len;
		}
		for ( int waited = 0 ; waited < cm_jobTestDelayMs && !cm_workerStop ; waited += 50 ) {
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );	// the test's slow disk
		}
		const auto t2 = std::chrono::steady_clock::now();
		if ( buf && CM_IsXbsz( buf, len ) ) {	// collision option C: the exact BSP back, then parsed as one
			int dlen = 0;
			byte *d = CM_XbszDecode( buf, len, &dlen, job->err, sizeof( job->err ) );
			if ( d ) {
				cm_transientBytes += dlen;
			}
			free( buf );
			cm_transientBytes -= len;
			buf = d;
			len = dlen;
		}
		const auto t2b = std::chrono::steady_clock::now();
		if ( buf ) {
			job->result = CM_ParseChunk( buf, len, job->name, job->origin, job->err, sizeof( job->err ) );
			free( buf );
			cm_transientBytes += ( job->result ? job->result->bytes : 0 ) - len;	// until the frame takes it
		}
		const auto t3 = std::chrono::steady_clock::now();
		job->readMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>( t1 - t0 ).count();
		job->decodeMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>( t2b - t2 ).count();
		job->parseMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>( t3 - t2b ).count();
		{
			std::lock_guard<std::mutex> lock( cm_jobLock );
			if ( job->generation == cm_jobGeneration ) {
				cm_jobDone.push_back( job );
				continue;
			}
		}
		// abandoned by a map change while it was parsed: no frame takes it (a client at the menu runs no residency),
		// so its blocks are freed here
		if ( job->result ) {
			const long long bytes = job->result->bytes;
			CM_FreeChunk( job->result );
			cm_transientBytes -= bytes;
		}
		free( job );
	}
}

static bool						cm_shutDown;	// CM_ChunkShutdown ran: no worker again

static void CM_StartWorker( void )
{
	if ( !cm_worker && !cm_shutDown ) {
		cm_workerStop = false;
		cm_worker = new std::thread( CM_ChunkWorker );
	}
}

/*
==================
CM_ChunkShutdown

At the engine's shutdown: the worker frees what it holds, stops and is joined, so the statics it uses outlive it (a
detached worker blocked in its condition variable can hang the exit on glibc)
==================
*/
void CM_ChunkShutdown( void )
{
	cm_shutDown = true;
	if ( !cm_worker ) {
		return;
	}
	{
		// set under the lock: a worker that has just found the predicate false and not yet blocked would otherwise
		// miss the notify and sleep through the join)
		std::lock_guard<std::mutex> stopLock( cm_jobLock );
		cm_workerStop = true;
	}
	cm_jobCv.notify_all();
	cm_worker->join();
	delete cm_worker;
	cm_worker = NULL;
	std::lock_guard<std::mutex> lock( cm_jobLock );
	for ( cmJob_t *job : cm_jobQueue ) {
		free( job );
	}
	cm_jobQueue.clear();
	for ( cmJob_t *job : cm_jobDone ) {
		if ( job->result ) {
			CM_FreeChunk( job->result );
		}
		free( job );
	}
	cm_jobDone.clear();
	for ( cmChunk_t *c : cm_freeQueue ) {
		CM_FreeChunk( c );
	}
	cm_freeQueue.clear();
}

// main thread: stage a manifest chunk for the worker. qfalse when its pack cannot be named (then it loads in place)
static qboolean CM_QueueChunk( int slot, const char *name, const vec3_t origin, float dist )
{
	cmJob_t *job = (cmJob_t *)calloc( 1, sizeof( *job ) );
	if ( !job || !FS_PakPathOf( name, job->pak, sizeof( job->pak ) ) ) {
		free( job );
		return qfalse;
	}
	job->slot = slot;
	Q_strncpyz( job->name, name, sizeof( job->name ) );
	VectorCopy( origin, job->origin );
	job->dist = dist;
	cm_slotPending[slot] = 1;
	cm_jobStaged.push_back( job );
	return qtrue;		// staged: the frame queues all its jobs at once (CM_FlushChunkJobs)
}

// main thread, after a frame's CM_QueueChunk calls: its jobs into the queue under one lock, then the worker started
// or woken, so its nearest-first pick sees them all (started on the first job, it picked among one or two: run 5)
static void CM_FlushChunkJobs( void )
{
	if ( cm_jobStaged.empty() ) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock( cm_jobLock );
		for ( cmJob_t *job : cm_jobStaged ) {
			job->generation = cm_jobGeneration;
			cm_jobQueue.push_back( job );
		}
	}
	cm_jobStaged.clear();
	CM_StartWorker();
	cm_jobCv.notify_one();
}

// main thread: a chunk out of its slot is freed by the worker (the frame does not wait on freeing hundreds of MB: run
// 6's 77 ms), or here once the worker is shut down
static void CM_RetireChunk( cmChunk_t *c )
{
	if ( cm_shutDown ) {
		CM_FreeChunk( c );
		return;
	}
	{
		std::lock_guard<std::mutex> lock( cm_jobLock );
		cm_freeQueue.push_back( c );
		cm_transientBytes += c->bytes;		// until the worker has freed it
	}
	CM_StartWorker();
	cm_jobCv.notify_one();
}

// the one accounting path for a slot's chunk (every load, swap and eviction goes through these two)
static void CM_SlotPut( int slot, cmChunk_t *c )
{
	cm_chunks[slot] = c;
	if ( slot < cm_manifestCount ) {
		const cmManifestEntry_t *e = &cm_manifest[slot];
		for ( int k = 0 ; k < 2 ; k++ ) {
			if ( c->mins[k] < e->mins[k] - 2 || c->maxs[k] > e->maxs[k] + 2 ) {
				cm_slotAlways[slot] = 1;	// its own bounds go past the index's: never missed by a query
			}
		}
	}
	if ( slot >= cm_numChunks ) {
		cm_numChunks = slot + 1;
	}
	cm_res.resident++;
	cm_res.bytes += c->bytes;
	if ( cm_res.resident > cm_res.peakResident ) {
		cm_res.peakResident = cm_res.resident;
	}
	if ( cm_res.bytes > cm_res.peakBytes ) {
		cm_res.peakBytes = cm_res.bytes;
	}
}

static void CM_SlotEvict( int slot )
{
	cmChunk_t *c = cm_chunks[slot];

	if ( c ) {
		cm_chunks[slot] = NULL;
		cm_res.resident--;
		cm_res.bytes -= c->bytes;
		cm_res.evictions++;
		CM_RetireChunk( c );
	}
}

// main thread: the worker's finished jobs, taken off its list (residency swaps them in or retires them)
static std::vector<cmJob_t *> CM_TakeDoneJobs( void )
{
	std::vector<cmJob_t *> done;
	std::lock_guard<std::mutex> lock( cm_jobLock );
	done.swap( cm_jobDone );
	for ( const cmJob_t *job : done ) {
		cm_transientBytes -= job->result ? job->result->bytes : 0;	// the frame's now: a slot's, or retired (counted again)
	}
	return done;
}

// main thread: finished jobs no one will swap in (a map change, no manifest, no drivers) retired, so their blocks are
// freed
static void CM_DiscardDoneJobs( void )
{
	for ( cmJob_t *job : CM_TakeDoneJobs() ) {
		if ( job->generation == cm_jobGeneration ) {
			cm_slotPending[job->slot] = 0;
		}
		if ( job->result ) {
			CM_RetireChunk( job->result );
		}
		free( job );
	}
}

// main thread, at a map change: every queued job is dropped, every finished one retired, and a job in flight is
// retired when it finishes (its generation is old)
static void CM_AbandonChunkJobs( void )
{
	{
		std::lock_guard<std::mutex> lock( cm_jobLock );
		for ( cmJob_t *job : cm_jobQueue ) {
			free( job );
		}
		cm_jobQueue.clear();
		cm_jobGeneration++;
	}
	for ( cmJob_t *job : cm_jobStaged ) {
		free( job );
	}
	cm_jobStaged.clear();
	CM_DiscardDoneJobs();
	memset( cm_slotPending, 0, sizeof( cm_slotPending ) );
}

/*
==================
CM_LoadChunkSlot

A collision chunk (cm_local.h) loaded now on the main thread: its BSP read through the file system (its search order,
loose files first where the setup allows them, the pack marked referenced) and parsed by CM_ParseChunk, the worker's
own parser, so a chunk is only ever malloc'd blocks of one validated shape. NULL, with a warning, on failure; a bad
chunk never drops the map
==================
*/
static cmChunk_t *CM_LoadChunkSlot( int slot, const char *name, const vec3_t origin )
{
	char	err[160] = "";
	void	*buf = NULL;

	cm_lastFailTransient = false;
	if ( !cmg.numNodes ) {
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: no map loaded\n", name );
		return NULL;
	}
	for ( int i = 0 ; i < 3 ; i++ ) {
		// whole units keep a query moved into the chunk's frame exact
		if ( origin[i] != floorf( origin[i] ) || !std::isfinite( origin[i] ) ) {
			Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: origin %f %f %f is not whole units\n", name, origin[0],
				origin[1], origin[2] );
			return NULL;
		}
	}
	static cvar_t *testFailMem;
	if ( !testFailMem ) {
		testFailMem = Cvar_Get( "cm_chunkTestFailMem", "0", CVAR_CHEAT, "Test fault: the next N chunk loads fail as out of memory" );	// a test-only fault: the next N fail as OOM
	}
	if ( testFailMem->integer > 0 ) {
		Cvar_SetValue( "cm_chunkTestFailMem", testFailMem->integer - 1 );
		cm_lastFailTransient = true;
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: out of memory (cm_chunkTestFailMem)\n", name );
		return NULL;
	}
	const int t0 = Sys_Milliseconds();
	const int len = FS_ReadFile( name, &buf );
	if ( len <= 0 || !buf ) {
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s not found\n", name );
		return NULL;
	}
	cmChunk_t *c = NULL;
	if ( CM_IsXbsz( (const byte *)buf, len ) ) {	// collision option C: decoded to the exact BSP first
		int dlen = 0;
		byte *d = CM_XbszDecode( (const byte *)buf, len, &dlen, err, sizeof( err ) );
		FS_FreeFile( buf );
		if ( d ) {
			c = CM_ParseChunk( d, dlen, name, origin, err, sizeof( err ) );
			free( d );
		}
	} else {
		c = CM_ParseChunk( (byte *)buf, len, name, origin, err, sizeof( err ) );
		FS_FreeFile( buf );
	}
	if ( !c ) {
		// out of memory passes (a full box, the preload's peak): retried later, not given up
		cm_lastFailTransient = !strncmp( err, "out of memory", 13 );
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: %s\n", name, err );
		return NULL;
	}
	CM_SlotPut( slot, c );
	// the load time sizes the residency radius (docs/levelport-collision.md section 16)
	CM_LoadPrint()( "CM_LoadChunk: %s at %.0f %.0f %.0f: %d brushes, %d ms, in place\n", name, origin[0], origin[1],
		origin[2], c->cm.numBrushes, Sys_Milliseconds() - t0 );
	return c;
}

void CM_ClearChunks( void )
{
	// the map's chunks, the manifest, the worker's jobs and the drivers go with the map; so does the idle clock, or an
	// empty server's next map would evict its spawn preload on its first frame
	cm_idleSince = 0;
	CM_AbandonChunkJobs();
	for ( int i = 0 ; i < cm_numChunks ; i++ ) {
		if ( cm_chunks[i] ) {
			CM_RetireChunk( cm_chunks[i] );
		}
	}
	memset( cm_chunks, 0, sizeof( cm_chunks ) );
	memset( cm_pinned, 0, sizeof( cm_pinned ) );
	memset( cm_keepFrames, 0, sizeof( cm_keepFrames ) );
	memset( cm_slotDead, 0, sizeof( cm_slotDead ) );
	memset( cm_slotRetryAt, 0, sizeof( cm_slotRetryAt ) );
	memset( cm_lastWanted, 0, sizeof( cm_lastWanted ) );
	cm_numChunks = 0;
	if ( cm_manifest ) {
		Z_Free( cm_manifest );
		cm_manifest = NULL;
	}
	cm_manifestCount = 0;
	cm_numDrives = 0;
	memset( &cm_res, 0, sizeof( cm_res ) );
	cm_pendingStops = cm_pendingStopsHeld = cm_pendingStopsShort = 0;
	cm_heldNow = qfalse;
	cm_numUnloaded = 0;
	memset( cm_slotSolid, 0, sizeof( cm_slotSolid ) );
	CM_FreeChunkGrid();
}

/*
==================
CM_ListUnloaded

The manifest chunks not swapped in, for traces and point contents to treat as solid (cm_chunkUnloadedSolid 1, the
default; section 23), a given-up chunk too. Called whenever a slot changes: residency's frame, an in-place load
==================
*/
static void CM_ListUnloaded( void )
{
	static cvar_t	*solid;

	if ( !solid ) {
		solid = Cvar_Get( "cm_chunkUnloadedSolid", "1", CVAR_CHEAT, "Traces treat a collision chunk not loaded yet as a solid box (0 = empty space)" );
	}
	int n = 0;
	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		cm_slotSolid[i] = solid->integer && !cm_chunks[i] ? ( cm_slotPending[i] ? 2 : 1 ) : 0;	// 2: loading
		if ( cm_slotSolid[i] ) {
			VectorCopy( cm_manifest[i].mins, cm_unloadedMins[i] );
			VectorCopy( cm_manifest[i].maxs, cm_unloadedMaxs[i] );
			n++;
		}
	}
	cm_numUnloaded = n;
}

/*
==================
CM_BuildChunkGrid / CM_ChunkCandidates

At the manifest: each slot listed in every cell its placed bounds, widened 2 u, overlap (x and y). A query visits the
cells its box covers and returns their slots once each, ascending, with every always-visited slot and every slot past
the manifest, so traces see the chunks in the order they did before the index
==================
*/
static void CM_FreeChunkGrid( void )
{
	if ( cm_grid.cellStart ) {
		Z_Free( cm_grid.cellStart );
	}
	if ( cm_grid.slots ) {
		Z_Free( cm_grid.slots );
	}
	memset( &cm_grid, 0, sizeof( cm_grid ) );
	memset( cm_slotAlways, 0, sizeof( cm_slotAlways ) );
}

static void CM_GridCells( const vec3_t lo, const vec3_t hi, int *cx0, int *cy0, int *cx1, int *cy1 )
{
	*cx0 = (int)floorf( ( lo[0] - cm_grid.x0 ) / CM_GRID_CELL );
	*cy0 = (int)floorf( ( lo[1] - cm_grid.y0 ) / CM_GRID_CELL );
	*cx1 = (int)floorf( ( hi[0] - cm_grid.x0 ) / CM_GRID_CELL );
	*cy1 = (int)floorf( ( hi[1] - cm_grid.y0 ) / CM_GRID_CELL );
	*cx0 = *cx0 < 0 ? 0 : *cx0;
	*cy0 = *cy0 < 0 ? 0 : *cy0;
	*cx1 = *cx1 >= cm_grid.nx ? cm_grid.nx - 1 : *cx1;
	*cy1 = *cy1 >= cm_grid.ny ? cm_grid.ny - 1 : *cy1;
}

static void CM_BuildChunkGrid( void )
{
	CM_FreeChunkGrid();
	if ( !cm_manifestCount ) {
		return;
	}
	float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		x0 = std::min( x0, cm_manifest[i].mins[0] - 2 );
		y0 = std::min( y0, cm_manifest[i].mins[1] - 2 );
		x1 = std::max( x1, cm_manifest[i].maxs[0] + 2 );
		y1 = std::max( y1, cm_manifest[i].maxs[1] + 2 );
	}
	// extreme (finite) manifest bounds would overflow the cell counts or their product: then no grid, and every slot
	// is a candidate (CM_ChunkCandidates), as before the index
	const double gx = ( (double)x1 - x0 ) / CM_GRID_CELL + 1, gy = ( (double)y1 - y0 ) / CM_GRID_CELL + 1;
	if ( !( gx >= 1 && gy >= 1 && gx * gy <= 4194304.0 ) ) {
		Com_Printf( S_COLOR_YELLOW "cm_chunkmanifest: bounds span %.0f x %.0f cells: no chunk index, every chunk a candidate\n",
					gx, gy );
		return;
	}
	cm_grid.x0 = x0;
	cm_grid.y0 = y0;
	cm_grid.nx = (int)( ( x1 - x0 ) / CM_GRID_CELL ) + 1;		// the grid as it always was; the check above in double
	cm_grid.ny = (int)( ( y1 - y0 ) / CM_GRID_CELL ) + 1;
	const int cells = cm_grid.nx * cm_grid.ny;
	std::vector<int> count( cells, 0 );
	for ( int pass = 0 ; pass < 2 ; pass++ ) {
		if ( pass == 1 ) {
			cm_grid.cellStart = (int *)Z_Malloc( ( cells + 1 ) * sizeof( int ), TAG_GENERAL, qtrue );
			for ( int c = 0 ; c < cells ; c++ ) {
				cm_grid.cellStart[c + 1] = cm_grid.cellStart[c] + count[c];
			}
			cm_grid.slots = (int *)Z_Malloc( ( cm_grid.cellStart[cells] + 1 ) * sizeof( int ), TAG_GENERAL, qtrue );
			std::fill( count.begin(), count.end(), 0 );
		}
		for ( int i = 0 ; i < cm_manifestCount ; i++ ) {	// ascending: each cell's list is in slot order
			vec3_t lo, hi;
			VectorSet( lo, cm_manifest[i].mins[0] - 2, cm_manifest[i].mins[1] - 2, 0 );
			VectorSet( hi, cm_manifest[i].maxs[0] + 2, cm_manifest[i].maxs[1] + 2, 0 );
			int cx0, cy0, cx1, cy1;
			CM_GridCells( lo, hi, &cx0, &cy0, &cx1, &cy1 );
			for ( int cy = cy0 ; cy <= cy1 ; cy++ ) {
				for ( int cx = cx0 ; cx <= cx1 ; cx++ ) {
					const int c = cy * cm_grid.nx + cx;
					if ( pass == 1 ) {
						cm_grid.slots[cm_grid.cellStart[c] + count[c]] = i;
					}
					count[c]++;
				}
			}
		}
	}
}

int CM_ChunkCandidates( const vec3_t lo, const vec3_t hi, int *out )
{
	int n = 0;

	if ( !cm_grid.cellStart ) {				// no manifest: every slot, as before the index
		for ( int i = 0 ; i < cm_numChunks ; i++ ) {
			out[n++] = i;
		}
		return n;
	}
	if ( ++cm_candSeq == 0x7fffffff ) {
		memset( cm_candStamp, 0, sizeof( cm_candStamp ) );
		cm_candSeq = 1;
	}
	if ( hi[0] >= cm_grid.x0 && hi[1] >= cm_grid.y0 && lo[0] <= cm_grid.x0 + cm_grid.nx * CM_GRID_CELL &&
		 lo[1] <= cm_grid.y0 + cm_grid.ny * CM_GRID_CELL ) {
		int cx0, cy0, cx1, cy1;
		CM_GridCells( lo, hi, &cx0, &cy0, &cx1, &cy1 );
		for ( int cy = cy0 ; cy <= cy1 ; cy++ ) {
			for ( int cx = cx0 ; cx <= cx1 ; cx++ ) {
				const int c = cy * cm_grid.nx + cx;
				for ( int s = cm_grid.cellStart[c] ; s < cm_grid.cellStart[c + 1] ; s++ ) {
					const int i = cm_grid.slots[s];
					if ( cm_candStamp[i] != cm_candSeq ) {
						cm_candStamp[i] = cm_candSeq;
						out[n++] = i;
					}
				}
			}
		}
	}
	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		if ( cm_slotAlways[i] && cm_candStamp[i] != cm_candSeq ) {
			cm_candStamp[i] = cm_candSeq;
			out[n++] = i;
		}
	}
	for ( int i = cm_manifestCount ; i < cm_numChunks ; i++ ) {	// placed by hand past the manifest
		out[n++] = i;
	}
	std::sort( out, out + n );
	return n;
}

/*
==================
CM_LoadChunk

cm_loadchunk's chunk: the manifest's slot when the manifest names it at that origin, else a slot after the manifest's.
The same name at the same origin loads once. Returns its slot, or -1 when it could not be loaded
==================
*/
int CM_LoadChunk( const char *name, const vec3_t origin )
{
	int		i;

	for ( i = 0 ; i < cm_numChunks ; i++ ) {
		if ( cm_chunks[i] && !Q_stricmp( cm_chunks[i]->cm.name, name ) && VectorCompare( cm_chunks[i]->origin, origin ) ) {
			return i;
		}
	}
	for ( i = 0 ; i < cm_manifestCount ; i++ ) {
		if ( !Q_stricmp( cm_manifest[i].name, name ) && VectorCompare( cm_manifest[i].origin, origin ) ) {
			return CM_LoadChunkSlot( i, name, origin ) ? i : -1;
		}
	}
	i = cm_numChunks > cm_manifestCount ? cm_numChunks : cm_manifestCount;
	if ( i == MAX_CM_CHUNKS ) {
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: more than %d collision chunks\n", MAX_CM_CHUNKS );
		return -1;
	}
	return CM_LoadChunkSlot( i, name, origin ) ? i : -1;
}

// cm_loadchunk <bsp> <x> <y> <z>: places a collision chunk by hand (the coordinate test, the playtest hub's cfg)
void CM_LoadChunk_f( void )
{
	vec3_t	origin;

	if ( Cmd_Argc() != 5 ) {
		Com_Printf( "usage: cm_loadchunk <maps/name.bsp> <x> <y> <z>\n" );
		return;
	}
	origin[0] = atof( Cmd_Argv( 2 ) );
	origin[1] = atof( Cmd_Argv( 3 ) );
	origin[2] = atof( Cmd_Argv( 4 ) );
	Com_Printf( "cm_loadchunk: %s at %s %s %s: chunk %d\n", Cmd_Argv( 1 ), Cmd_Argv( 2 ), Cmd_Argv( 3 ), Cmd_Argv( 4 ),
		CM_LoadChunk( Cmd_Argv( 1 ), origin ) );
	CM_ListUnloaded();
}

/*
==================
CM_ReadChunkManifest

A manifest: every chunk of the map, a line each, "<bsp> <ox> <oy> <oz> <minx> <miny> <minz> <maxx> <maxy> <maxz>"
(whole-unit origin, placed bounds; hub/make_chunks_pk3.py writes it). A line with a non-whole origin or bounds not
finite and ordered is skipped with a warning. The regions around the spawns load in place; the rest is residency's.
qfalse when the file is missing
==================
*/
static qboolean CM_ReadChunkManifest( const char *file, bool preload = true )
{
	char	*buf, *p, *line;
	int		len, n, skipped = 0, over = 0;

	len = FS_ReadFile( file, (void **)&buf );
	if ( len <= 0 || !buf ) {
		return qfalse;
	}
	cm_manifest = (cmManifestEntry_t *)Z_Malloc( MAX_CM_CHUNKS * sizeof( *cm_manifest ), TAG_GENERAL, qtrue );
	n = 0;
	for ( p = buf ; *p ; ) {
		line = p;
		while ( *p && *p != '\n' ) {
			p++;
		}
		if ( *p ) {
			*p++ = 0;
		}
		cmManifestEntry_t *e = &cm_manifest[n];
		if ( line[0] == '/' || line[0] == '#' || !line[0] ) {
			continue;
		}
		if ( sscanf( line, "%63s %f %f %f %f %f %f %f %f %f", e->name, &e->origin[0], &e->origin[1], &e->origin[2],
				&e->mins[0], &e->mins[1], &e->mins[2], &e->maxs[0], &e->maxs[1], &e->maxs[2] ) != 10 ) {
			continue;
		}
		qboolean ok = qtrue;
		for ( int k = 0 ; k < 3 ; k++ ) {
			ok = ok && std::isfinite( e->origin[k] ) && e->origin[k] == floorf( e->origin[k] ) && std::isfinite( e->mins[k] ) &&
				std::isfinite( e->maxs[k] ) && e->mins[k] <= e->maxs[k] ? qtrue : qfalse;
		}
		if ( !ok ) {
			Com_Printf( S_COLOR_YELLOW "cm_chunkmanifest: %s: %s skipped (origin not whole units, or bad bounds)\n", file,
				e->name );
			skipped++;
			continue;
		}
		if ( ++n == MAX_CM_CHUNKS ) {
			for ( ; *p ; ) {	// the lines past the limit, counted for the warning
				line = p;
				while ( *p && *p != '\n' ) {
					p++;
				}
				if ( *p ) {
					*p++ = 0;
				}
				if ( line[0] && line[0] != '/' && line[0] != '#' && line[0] != '\r' ) {
					over++;
				}
			}
			break;
		}
	}
	FS_FreeFile( buf );
	if ( !n ) {	// no valid line: no table (the guards on cm_manifestCount would let the next read allocate another)
		Z_Free( cm_manifest );
		cm_manifest = NULL;
	}
	cm_manifestCount = n;
	CM_BuildChunkGrid();
	cm_res.nearestUnloaded = cm_res.nearestPending = 1e30f;
	Com_Printf( "cm_chunkmanifest: %s: %d chunks%s\n", file, n, skipped ? " (some skipped)" : "" );
	if ( over ) {
		Com_Printf( S_COLOR_RED "cm_chunkmanifest: %s: %d lines past the limit of %d chunks were not read: that part of the "
			"map has no collision\n", file, over, MAX_CM_CHUNKS );
	}
	if ( preload ) {
		CM_PreloadSpawns();
	}
	return qtrue;
}

// cm_chunkmanifest <file>: a manifest by hand (a dev tool; a map's own maps/<map>.chunks is read at its load)
void CM_ChunkManifest_f( void )
{
	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: cm_chunkmanifest <file>\n" );
		return;
	}
	if ( !cmg.numNodes ) {
		Com_Printf( "cm_chunkmanifest: no map loaded\n" );
		return;
	}
	if ( cm_manifestCount || cm_numChunks ) {
		Com_Printf( "cm_chunkmanifest: chunks are already placed on this map\n" );
		return;
	}
	if ( !CM_ReadChunkManifest( Cmd_Argv( 1 ) ) ) {
		Com_Printf( "cm_chunkmanifest: %s not found\n", Cmd_Argv( 1 ) );
	}
}

/*
==================
CM_LoadMapChunks

At the map's load: its own manifest, maps/<map>.chunks beside maps/<map>.bsp, so a server cannot run a chunked map
without its chunks. cm_chunkManifest 0 skips it (a test's hub alone, or every chunk placed by hand). A client's load
(a remote client's: a listen server's manifest is read already) does not preload the spawn regions: it spawns nothing,
and its own residency streams the chunks around its player (the preload loaded 20-25 chunks in place
at every connect and evicted most on the first frame)
==================
*/
void CM_LoadMapChunks( const char *name, qboolean clientload )
{
	char	file[MAX_QPATH];

	if ( !cmg.numNodes || cm_manifestCount || cm_numChunks ||
		 !Cvar_Get( "cm_chunkManifest", "1", CVAR_ARCHIVE_ND, "Read a chunked map's maps/<map>.chunks manifest at map load; 0 = the hub alone (chunks placed by hand)" )->integer ) {
		return;
	}
	COM_StripExtension( name, file, sizeof( file ) );
	Q_strcat( file, sizeof( file ), ".chunks" );
	CM_ReadChunkManifest( file, !clientload );
}

// cm_drive <x> <y> <z> | clear: a virtual driver for residency (a test's player); several may stand at once
void CM_Drive_f( void )
{
	if ( Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "clear" ) ) {
		cm_numDrives = 0;
		return;
	}
	if ( Cmd_Argc() != 4 ) {
		Com_Printf( "usage: cm_drive <x> <y> <z> | cm_drive clear\n" );
		return;
	}
	if ( cm_numDrives == MAX_CM_DRIVES ) {
		cm_numDrives = 0;
	}
	VectorSet( cm_drives[cm_numDrives], atof( Cmd_Argv( 1 ) ), atof( Cmd_Argv( 2 ) ), atof( Cmd_Argv( 3 ) ) );
	cm_numDrives++;
}

// cm_drive_move <x> <y> <z>: moves the first virtual driver (a test's walk), or places it
void CM_DriveMove_f( void )
{
	if ( Cmd_Argc() != 4 ) {
		Com_Printf( "usage: cm_drive_move <x> <y> <z>\n" );
		return;
	}
	VectorSet( cm_drives[0], atof( Cmd_Argv( 1 ) ), atof( Cmd_Argv( 2 ) ), atof( Cmd_Argv( 3 ) ) );
	if ( !cm_numDrives ) {
		cm_numDrives = 1;
	}
}

static int CM_SortInt( const void *a, const void *b )
{
	return *(const int *)a - *(const int *)b;
}

static int CM_SpawnHoldMedian( void )
{
	int		v[ARRAY_LEN( cm_res.spawnHoldMs )];
	const int n = cm_res.spawnHolds < (int)ARRAY_LEN( v ) ? cm_res.spawnHolds : (int)ARRAY_LEN( v );

	if ( !n ) {
		return 0;
	}
	memcpy( v, cm_res.spawnHoldMs, n * sizeof( v[0] ) );
	qsort( v, n, sizeof( v[0] ), CM_SortInt );
	return v[n / 2];
}

// cm_chunkreport [reset]: residency's counters since the manifest was read (reset: from now, a test's walk)
static float CM_SpawnPreloadRadius( float *holdOut );
static long long CM_ChunkCapBytes( void );

void CM_ChunkReport_f( void )
{
	float	hold;
	const float preloadRadius = CM_SpawnPreloadRadius( &hold );

	if ( Cmd_Argc() == 2 && !Q_stricmp( Cmd_Argv( 1 ), "reset" ) ) {
		const int resident = cm_res.resident;
		const long long bytes = cm_res.bytes;
		memset( &cm_res, 0, sizeof( cm_res ) );
		cm_pendingStops = cm_pendingStopsHeld = cm_pendingStopsShort = 0;
		cm_res.nearestUnloaded = cm_res.nearestPending = 1e30f;
		cm_res.resident = cm_res.peakResident = resident;
		cm_res.bytes = cm_res.peakBytes = bytes;
		return;
	}
	Com_Printf( "cm_chunkreport: frames %d, loads %d, evictions %d, violations %d, held %d (first %d, last %d), "
		"resident %d (peak %d), bytes %lld (peak %lld), worst residency %d ms (%d loads), worst server frame %d ms, "
		"read max %d ms (sum %d), parse max %d ms (sum %d), failed %d, preloads %d (%d ms), spawn holds %d "
		"(median %d ms, max %d ms), false walls %d (nearest unloaded %.0f u)\n", cm_res.frames, cm_res.loads,
		cm_res.evictions, cm_res.violations, cm_res.heldFrames, cm_res.firstHeld, cm_res.lastHeld, cm_res.resident,
		cm_res.peakResident, cm_res.bytes, cm_res.peakBytes, cm_res.worstFrameMs, cm_res.worstFrameLoads,
		cm_res.worstServerFrameMs, cm_res.maxReadMs, cm_res.sumReadMs, cm_res.maxParseMs, cm_res.sumParseMs,
		cm_res.failed, cm_res.preloads, cm_res.preloadMs, cm_res.spawnHolds, CM_SpawnHoldMedian(),
		cm_res.maxSpawnHoldMs, cm_res.falseWallFrames, cm_res.nearestUnloaded > 1e29f ? -1.0f : cm_res.nearestUnloaded );
	Com_Printf( "cm_chunkreport2: client held %d, near pending %d (nearest pending %.0f u), dead %d, cap evictions %d, "
		"cap misses %d, last spawn hold %d ms (%d chunks missing, %d loads), pending stops %d (%d while held, %d short), "
		"cancelled %d, spawned with missing %d, spawned beside dead %d, cap %lld MB, peak transient %lld MB, "
		"preload queued %d, preload cap skips %d, preload radius %.0f u, hold %.0f u\n", cm_res.clientHeldFrames, cm_res.nearPendingFrames, cm_res.nearestPending > 1e29f ? -1.0f : cm_res.nearestPending,
		cm_res.dead, cm_res.capEvictions, cm_res.capMisses, cm_res.lastSpawnHoldMs, cm_res.lastSpawnWaited,
		cm_res.lastSpawnLoads, cm_pendingStops, cm_pendingStopsHeld, cm_pendingStopsShort, cm_res.cancelled,
		cm_res.spawnedMissing, cm_res.spawnedBesideDead, CM_ChunkCapBytes() >> 20,
		cm_res.peakTransient >> 20, cm_res.preloadQueued, cm_res.preloadCapSkips, preloadRadius, hold );
	Com_Printf( "cm_chunkreport3: decode max %d ms (sum %d)\n", cm_res.maxDecodeMs, cm_res.sumDecodeMs );
}

// the server frame's length, start to end (sv_main): the async gate's stall
void CM_NoteServerFrame( int ms )
{
	if ( ms > cm_res.worstServerFrameMs ) {
		cm_res.worstServerFrameMs = ms;
	}
}

// a client held this frame (sv_main): counted beside the virtual drivers' held frames
void CM_NoteClientHeld( void )
{
	cm_res.clientHeldFrames++;
	cm_heldNow = qtrue;
}

static float CM_BoundsDist2D( const vec3_t p, const vec3_t mins, const vec3_t maxs )
{
	const float dx = p[0] < mins[0] ? mins[0] - p[0] : ( p[0] > maxs[0] ? p[0] - maxs[0] : 0.0f );
	const float dy = p[1] < mins[1] ? mins[1] - p[1] : ( p[1] > maxs[1] ? p[1] - maxs[1] : 0.0f );
	return sqrtf( dx * dx + dy * dy );
}

// the chunk cvars more than one place reads, each registered in one place
static cvar_t *CM_ChunkLoadDistCvar( void )
{
	static cvar_t	*loadDist;

	if ( !loadDist ) {
		loadDist = Cvar_Get( "cm_chunkLoadDist", "9600", CVAR_ARCHIVE_ND, "A collision chunk loads when a driver is within this many units of its bounds in x and y" );
	}
	return loadDist;
}

static cvar_t *CM_ChunkAsyncCvar( void )
{
	static cvar_t	*async;

	if ( !async ) {
		async = Cvar_Get( "cm_chunkAsync", "1", CVAR_ARCHIVE_ND, "Collision chunks load on a worker thread (1) or in place in the server frame (0)" );
	}
	return async;
}

cvar_t *CM_ChunkSpawnNearCvar( void )
{
	static cvar_t	*spawnNear;

	if ( !spawnNear ) {
		spawnNear = Cvar_Get( "cm_chunkSpawnNear", "2000", CVAR_ARCHIVE_ND, "A spawning client is held until the chunks within this many units are loaded (0 = the whole cm_chunkLoadDist region)" );
	}
	return spawnNear;
}

cvar_t *CM_ChunkFirstEntryNearCvar( void )
{
	static cvar_t	*firstNear;

	if ( !firstNear ) {
		firstNear = Cvar_Get( "sv_chunkFirstEntryNear", "1", CVAR_ARCHIVE_ND, "A client's first entry waits only for the chunks within cm_chunkSpawnNear, as a respawn does (0 = the whole load region)" );
	}
	return firstNear;
}

static float CM_ChunkLoadDist( void )
{
	return CM_ChunkLoadDistCvar()->value;
}

/*
==================
CM_SlotFailed

A slot whose load failed in place: given up when the file is missing or bad (it stays a solid box and holds no one),
tried again in 5 s when the failure was memory (a small box's spawn preload must not leave dead
chunks behind once the memory is back)
==================
*/
static void CM_SlotFailed( int i, const char *name )
{
	if ( cm_lastFailTransient ) {
		cm_slotRetryAt[i] = Sys_Milliseconds() + 5000;
		cm_res.failed++;
		Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: out of memory, tried again in 5 s\n", name );
		return;
	}
	cm_slotDead[i] = 1;
	cm_res.dead++;
	Com_Printf( S_COLOR_RED "CM_LoadChunk: %s cannot be loaded: it stays solid and holds no one\n", name );
}

// a slot residency or the preload may try: not given up, and past a transient failure's wait
static bool CM_SlotTryNow( int i )
{
	return !cm_slotDead[i] && Sys_Milliseconds() >= cm_slotRetryAt[i];
}

/*
==================
CM_ChunkCapBytes

cm_chunkMegs as bytes: the cap on the chunks' memory, resident and transient (cm_transientBytes) together. -1, the
default: 45% of the machine's physical memory, at least 512 MB and at most 1 GB in a 32-bit build (none when the
machine's is unknown); 0: no cap; more:
that many MB (a 1-4 GB server had no bound by default)
==================
*/
static long long CM_ChunkCapBytes( void )
{
	static cvar_t	*megs;
	static int		physMB = -1;

	if ( !megs ) {
		megs = Cvar_Get( "cm_chunkMegs", "-1", CVAR_ARCHIVE_ND, "Memory cap on collision chunks in MB: -1 = 45% of physical memory (at least 512; at most 1024 in a 32-bit build), 0 = no cap" );
	}
	if ( megs->integer > 0 ) {
		return (long long)megs->integer << 20;
	}
	if ( megs->integer < 0 ) {
		if ( physMB < 0 ) {
			physMB = Sys_PhysicalMemoryMB();
		}
		if ( physMB > 0 ) {
			long long capMB = std::max( 512LL, physMB * 45LL / 100 );
			if ( sizeof( void * ) == 4 ) {	// a 32-bit process's address space, shared with the hunk and the renderer
				capMB = std::min( capMB, 1024LL );
			}
			return capMB << 20;
		}
	}
	return 0;
}

// the chunks' memory counted against the cap: in their slots and outside them
static long long CM_ChunkBytesUsed( void )
{
	return cm_res.bytes + cm_transientBytes;
}

/*
==================
CM_ChunkPreload

Loads in place every manifest chunk within cm_chunkLoadDist of p, the region residency keeps around a driver there
(docs/levelport-collision.md section 20): a spawn at map load, a test's start. It waits; a spawn is not mid-play.
A pinned chunk is never evicted (cm_chunkPinSpawns 1; 0 by default: 0.77 GB on tag 1). A chunk the worker is reading
meanwhile is retired when it finishes (its slot is taken). Returns the chunks loaded (or queued).
With underOnly, only the chunks whose placed bounds hold p (the chunk under a spawn). Every chunk only while the
chunks' memory is under the cm_chunkMegs cap (the chunk under a spawn too; a
spawn whose set is left out waits for residency to load it, the hold's rule). With queue, given to the worker
(nearest p first) instead of loaded in place (the map load does not wait for them)
==================
*/
static int CM_ChunkPreload( const vec3_t p, qboolean pin, bool underOnly = false, float radius = -1.0f,
							bool queue = false )
{
	const float	ld = underOnly ? 0.0f : radius >= 0 ? radius : CM_ChunkLoadDist();
	const int	t0 = Sys_Milliseconds();
	const long long cap = CM_ChunkCapBytes();
	int			n = 0;

	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		const cmManifestEntry_t *e = &cm_manifest[i];
		const float d = CM_BoundsDist2D( p, e->mins, e->maxs );
		if ( d > ld ) {
			continue;
		}
		if ( pin ) {
			cm_pinned[i] = 1;
		}
		if ( cm_chunks[i] || ( queue && cm_slotPending[i] ) || !CM_SlotTryNow( i ) ) {
			continue;		// in place, a chunk the worker is reading is loaded anyway (its job is retired)
		}
		if ( cap && CM_ChunkBytesUsed() >= cap ) {
			cm_res.preloadCapSkips++;
			continue;
		}
		if ( queue && CM_QueueChunk( i, e->name, e->origin, d ) ) {
			cm_jobStaged.back()->preload = true;
			cm_res.preloadQueued++;
			n++;
		} else if ( CM_LoadChunkSlot( i, e->name, e->origin ) ) {
			cm_res.preloads++;
			n++;
		} else {
			CM_SlotFailed( i, e->name );
		}
	}
	cm_res.preloadMs += Sys_Milliseconds() - t0;
	return n;
}

/*
==================
CM_MapOrigins

The hub's entities by kind, from its entity string: spawn points (info_player_*, team_CTF_*spawn and team_CTF_*player)
and static items (item_*, weapon_*, ammo_*, holdable_* and team_CTF_*flag not suspended: spawnflags 1), whose first
think drops them to the floor with one trace and removes them when it starts solid (FinishSpawningItem)
==================
*/
static void CM_MapOrigins( std::vector<std::array<float, 3>> *spawns, std::vector<std::array<float, 3>> *statics )
{
	const char	*p = CM_EntityString();

	while ( p ) {
		char	classname[MAX_QPATH] = "", origin[MAX_QPATH] = "", key[MAX_QPATH];
		int		spawnflags = 0;
		char	*tok = COM_Parse( &p );

		if ( !p || tok[0] != '{' ) {
			break;
		}
		for ( ;; ) {
			tok = COM_Parse( &p );
			if ( !p || tok[0] == '}' ) {
				break;
			}
			Q_strncpyz( key, tok, sizeof( key ) );
			tok = COM_Parse( &p );
			if ( !p ) {
				break;
			}
			if ( !Q_stricmp( key, "classname" ) ) {
				Q_strncpyz( classname, tok, sizeof( classname ) );
			} else if ( !Q_stricmp( key, "origin" ) ) {
				Q_strncpyz( origin, tok, sizeof( origin ) );
			} else if ( !Q_stricmp( key, "spawnflags" ) ) {
				spawnflags = atoi( tok );
			}
		}
		Q_strlwr( classname );
		const bool spawn = !strncmp( classname, "info_player_", 12 ) || ( !strncmp( classname, "team_ctf_", 9 ) &&
			( strstr( classname, "spawn" ) || strstr( classname, "player" ) ) );
		const bool item = !( spawnflags & 1 ) && ( !strncmp( classname, "item_", 5 ) || !strncmp( classname, "weapon_", 7 ) ||
			!strncmp( classname, "ammo_", 5 ) || !strncmp( classname, "holdable_", 9 ) ||
			( !strncmp( classname, "team_ctf_", 9 ) && strstr( classname, "flag" ) ) );
		vec3_t	o;
		if ( ( spawn || item ) && sscanf( origin, "%f %f %f", &o[0], &o[1], &o[2] ) == 3 ) {
			( spawn ? spawns : statics )->push_back( { o[0], o[1], o[2] } );
		}
	}
}

/*
==================
CM_PreloadStatics

The chunks within 64 u (x and y) of every static item loaded in place and kept from eviction for 40 residency frames,
so its drop to the floor meets the chunk, not the chunk's unloaded box, which would remove it.
At the manifest (map load) and before map_restart's entities (sv_ccmds). Returns the chunks loaded
==================
*/
static int CM_PreloadStaticsAt( const std::vector<std::array<float, 3>> &statics )
{
	int		n = 0;

	for ( const auto &a : statics ) {
		const vec3_t o = { a[0], a[1], a[2] };
		for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
			const cmManifestEntry_t *e = &cm_manifest[i];
			if ( CM_BoundsDist2D( o, e->mins, e->maxs ) > 64 ) {
				continue;
			}
			cm_keepFrames[i] = 40;
			if ( !cm_chunks[i] && CM_SlotTryNow( i ) ) {
				if ( CM_LoadChunkSlot( i, e->name, e->origin ) ) {
					cm_res.preloads++;
					n++;
				} else {
					CM_SlotFailed( i, e->name );
				}
			}
		}
	}
	return n;
}

void CM_PreloadStatics( void )
{
	std::vector<std::array<float, 3>> spawns, statics;

	if ( !cm_manifestCount ) {
		return;
	}
	const int t0 = Sys_Milliseconds();
	CM_MapOrigins( &spawns, &statics );
	const int n = CM_PreloadStaticsAt( statics );
	cm_res.preloadMs += Sys_Milliseconds() - t0;
	if ( statics.size() ) {
		Com_Printf( "cm_chunkmanifest: static items: %d, %d chunks loaded, %d ms\n", (int)statics.size(), n,
			Sys_Milliseconds() - t0 );
	}
	CM_ListUnloaded();
}

/*
==================
CM_SpawnPreloadRadius

The spawn preload's radius: cm_chunkSpawnPreload, and never less than the set a first spawn's hold waits for
(cm_chunkSpawnNear with sv_chunkFirstEntryNear 1; the whole region within cm_chunkLoadDist with either at 0), so the
two cannot disagree (raising the hold's radius alone brought the first-spawn hold back). -1 (or a
hold of the whole region): the whole region. Its defaults are sv_client.cpp's, read by the same names
==================
*/
static float CM_SpawnPreloadRadius( float *holdOut )
{
	static cvar_t	*spawnPreload, *spawnNear, *firstNear;

	if ( !spawnPreload ) {
		spawnPreload = Cvar_Get( "cm_chunkSpawnPreload", "0", CVAR_ARCHIVE_ND, "Radius preloaded around each spawn point at map load, never less than the spawn hold's (-1 = the whole region)" );
		spawnNear = CM_ChunkSpawnNearCvar();
		firstNear = CM_ChunkFirstEntryNearCvar();
	}
	const float hold = firstNear->integer && spawnNear->value > 0 ? spawnNear->value : -1.0f;
	if ( holdOut ) {
		*holdOut = hold;
	}
	if ( spawnPreload->value < 0 || hold < 0 ) {
		return -1.0f;
	}
	return spawnPreload->value > hold ? spawnPreload->value : hold;
}

/*
==================
CM_PreloadSpawns

At the manifest (map load): the chunks around the hub's spawn points within CM_SpawnPreloadRadius (cm_chunkSpawnNear's
2,000 u by default: the set a spawn's hold waits for; -1: the whole region within cm_chunkLoadDist, as it was before), pinned with cm_chunkPinSpawns 1, so the first spawns find their hold's set resident, and the
chunks under its static items (CM_PreloadStatics), all of it under the cm_chunkMegs cap. With cm_chunkPreloadAsync 1
(0 by default) only the chunk under each spawn is loaded in place and the rest of each set goes to the worker, nearest
its spawn first, for residency to swap in with or without a client: the map load returned 1.0-1.2
s sooner on the joined hub, but the game's initialisation then traced against the boxes of the chunks still loading
(Bot2's world graph, built at map load: 2,196 traces stopped on them, 0 in place), so it is off
==================
*/
void CM_PreloadSpawns( void )
{
	static cvar_t	*pinSpawns, *async, *asyncPreload;
	const int		t0 = Sys_Milliseconds();
	int				spawns = 0, loaded = 0, queued = 0, pinned = 0;
	float			hold;

	if ( !pinSpawns ) {
		pinSpawns = Cvar_Get( "cm_chunkPinSpawns", "0", CVAR_ARCHIVE_ND, "The chunks preloaded around spawn points are pinned: never evicted" );
		async = CM_ChunkAsyncCvar();
		asyncPreload = Cvar_Get( "cm_chunkPreloadAsync", "0", CVAR_ARCHIVE_ND, "At map load only the chunk under each spawn loads in place and the rest go to the worker (0 = all in place: the game's map-load traces need them)" );
	}
	const float radius = CM_SpawnPreloadRadius( &hold );
	const int	skips0 = cm_res.preloadCapSkips;
	std::vector<std::array<float, 3>> origins, statics;
	CM_MapOrigins( &origins, &statics );
	spawns = (int)origins.size();
	// the chunk under every spawn first, then the rest of each set
	for ( int pass = 0 ; pass < 2 ; pass++ ) {
		const bool queue = pass == 1 && async->integer && asyncPreload->integer;
		for ( const auto &a : origins ) {
			vec3_t o = { a[0], a[1], a[2] };
			const int n = CM_ChunkPreload( o, pinSpawns->integer ? qtrue : qfalse, pass == 0, radius, queue );
			*( queue ? &queued : &loaded ) += n;
		}
	}
	CM_FlushChunkJobs();
	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		pinned += cm_pinned[i];
	}
	Com_Printf( "cm_chunkmanifest: spawn regions: %d spawns, %d chunks loaded, %d queued, %d pinned, %d ms "
		"(radius %.0f u, hold %.0f u)\n", spawns, loaded, queued, pinned, Sys_Milliseconds() - t0, radius, hold );
	if ( cm_res.preloadCapSkips > skips0 ) {
		Com_Printf( S_COLOR_YELLOW "cm_chunkmanifest: cm_chunkMegs cap %lld MB: %d spawn chunks left to residency\n",
			CM_ChunkCapBytes() >> 20, cm_res.preloadCapSkips - skips0 );
	}
	if ( statics.size() ) {
		const int t1 = Sys_Milliseconds();
		const int n = CM_PreloadStaticsAt( statics );
		cm_res.preloadMs += Sys_Milliseconds() - t1;
		Com_Printf( "cm_chunkmanifest: static items: %d, %d chunks loaded, %d ms\n", (int)statics.size(), n,
			Sys_Milliseconds() - t1 );
	}
	CM_ListUnloaded();
}

// cm_chunkpending: the manifest chunks queued or being read now (the tests: a trace ending on a box still loading)
void CM_ChunkPending_f( void )
{
	int	n = 0;

	Com_Printf( "cm_chunkpending:" );
	for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
		if ( cm_slotPending[i] && !cm_chunks[i] ) {
			Com_Printf( " %s", cm_manifest[i].name );
			n++;
		}
	}
	Com_Printf( " (%d)\n", n );
}

// cm_chunkpreload <x> <y> <z>: a start's region loaded in place, not pinned (a test's first position counts as a spawn)
void CM_ChunkPreload_f( void )
{
	vec3_t	p;

	if ( Cmd_Argc() != 4 ) {
		Com_Printf( "usage: cm_chunkpreload <x> <y> <z>\n" );
		return;
	}
	VectorSet( p, atof( Cmd_Argv( 1 ) ), atof( Cmd_Argv( 2 ) ), atof( Cmd_Argv( 3 ) ) );
	const int t0 = Sys_Milliseconds();
	const int n = CM_ChunkPreload( p, qfalse );
	CM_ListUnloaded();
	Com_Printf( "cm_chunkpreload: %d chunks loaded, %d ms\n", n, Sys_Milliseconds() - t0 );
}

// the manifest slots whose placed bounds may come within dist of p in x and y: the chunk index's candidates for the
// square around p (the readiness queries scanned the whole manifest per held client and command)
static int CM_ChunkCandidatesNear( const vec3_t p, float dist, int *out )
{
	vec3_t	lo, hi;

	if ( !cm_grid.cellStart ) {			// no index (CM_ChunkCandidates' fallback is the loaded slots only): every slot
		for ( int i = 0 ; i < cm_manifestCount ; i++ ) {
			out[i] = i;
		}
		return cm_manifestCount;
	}
	VectorSet( lo, p[0] - dist, p[1] - dist, 0 );
	VectorSet( hi, p[0] + dist, p[1] + dist, 0 );
	int n = CM_ChunkCandidates( lo, hi, out ), m = 0;
	for ( int j = 0 ; j < n ; j++ ) {
		if ( out[j] < cm_manifestCount ) {	// placed by hand past the manifest: no residency
			out[m++] = out[j];
		}
	}
	return m;
}

// the manifest chunks within dist of p (x and y, to their placed bounds; 0 or less: cm_chunkLoadDist) not swapped in:
// a client entering play there waits while any is (a given-up chunk does not hold anyone: CM_ChunksDeadNear counts those)
int CM_ChunksMissingNear( const vec3_t p, float dist )
{
	const float ld = dist > 0 ? dist : CM_ChunkLoadDist();
	int			cand[MAX_CM_CHUNKS], n = 0;
	const int	numCand = CM_ChunkCandidatesNear( p, ld, cand );

	for ( int j = 0 ; j < numCand ; j++ ) {
		const int i = cand[j];
		if ( !cm_chunks[i] && !cm_slotDead[i] && CM_BoundsDist2D( p, cm_manifest[i].mins, cm_manifest[i].maxs ) <= ld ) {
			n++;
		}
	}
	return n;
}

// the given-up chunks (solid boxes for good) within dist of p, as CM_ChunksMissingNear measures: a spawn released
// beside one is counted apart (a failed chunk must not read as a spawn with its set complete)
int CM_ChunksDeadNear( const vec3_t p, float dist )
{
	const float ld = dist > 0 ? dist : CM_ChunkLoadDist();
	int			cand[MAX_CM_CHUNKS], n = 0;
	const int	numCand = CM_ChunkCandidatesNear( p, ld, cand );

	for ( int j = 0 ; j < numCand ; j++ ) {
		const int i = cand[j];
		if ( !cm_chunks[i] && cm_slotDead[i] && CM_BoundsDist2D( p, cm_manifest[i].mins, cm_manifest[i].maxs ) <= ld ) {
			n++;
		}
	}
	return n;
}

// qfalse while any manifest chunk within cm_chunkLoadDist of p is not swapped in
qboolean CM_ChunkRegionReady( const vec3_t p )
{
	return CM_ChunksMissingNear( p, 0 ) ? qfalse : qtrue;
}

int CM_ChunkLoadCount( void )
{
	return cm_res.loads;
}

// a client's wait at a spawn, from its entering to the chunks it waits for being in (sv_client): how many were missing
// when it entered, how many loads landed meanwhile, and how many of its spawn set (cm_chunkSpawnNear) are still missing
// at the release (0 by the hold's own rule: counted), and how many of that set were given
// up (failed loads, solid boxes for good: a release the hold allows, counted apart)
void CM_NoteSpawnHold( int ms, int waited, int loads, int missingAtRelease, int deadAtRelease )
{
	if ( missingAtRelease > 0 ) {
		cm_res.spawnedMissing++;
	}
	if ( deadAtRelease > 0 ) {
		cm_res.spawnedBesideDead++;
	}
	if ( cm_res.spawnHolds < (int)ARRAY_LEN( cm_res.spawnHoldMs ) ) {
		cm_res.spawnHoldMs[cm_res.spawnHolds] = ms;
	}
	cm_res.spawnHolds++;
	if ( ms > cm_res.maxSpawnHoldMs ) {
		cm_res.maxSpawnHoldMs = ms;
	}
	cm_res.lastSpawnHoldMs = ms;
	cm_res.lastSpawnWaited = waited;
	cm_res.lastSpawnLoads = loads;
}

/*
==================
CM_ChunkReadyAt

qfalse while a manifest chunk whose placed bounds (x and y, spread 64 u) hold p is not swapped in: a client standing
there is held (sv_client), not dropped through a floor that is not loaded yet; a remote client's prediction is held the
same way (cl_cgame). A given-up chunk holds no one
==================
*/
qboolean CM_HasChunks( void )
{
	return ( cm_manifestCount || cm_numChunks ) ? qtrue : qfalse;
}

qboolean CM_ChunkReadyAt( const vec3_t p )
{
	int			cand[MAX_CM_CHUNKS];
	const int	numCand = CM_ChunkCandidatesNear( p, 64, cand );

	for ( int j = 0 ; j < numCand ; j++ ) {
		const cmManifestEntry_t *e = &cm_manifest[cand[j]];
		const int i = cand[j];
		if ( !cm_chunks[i] && !cm_slotDead[i] && p[0] >= e->mins[0] - 64 && p[0] <= e->maxs[0] + 64 &&
			 p[1] >= e->mins[1] - 64 && p[1] <= e->maxs[1] + 64 ) {
			return qfalse;
		}
	}
	return qtrue;
}

// a finished job no driver wants any more, or a failed one: its slot no longer pending (current generation only)
static void CM_JobLanded( const cmJob_t *job )
{
	if ( job->generation == cm_jobGeneration ) {
		cm_slotPending[job->slot] = 0;
	}
}

// main thread, with no driver: the worker's finished jobs, the spawn preload's swapped into their slots while under the
// cm_chunkMegs cap (a failed one loaded once in place, then given up), every other one retired (CM_DiscardDoneJobs)
static void CM_SwapPreloadJobs( void )
{
	const long long cap = CM_ChunkCapBytes();

	for ( cmJob_t *job : CM_TakeDoneJobs() ) {
		const bool current = job->generation == cm_jobGeneration;
		CM_JobLanded( job );
		if ( !current || !job->preload || cm_chunks[job->slot] ) {
			if ( job->result ) {
				CM_RetireChunk( job->result );
			}
		} else if ( !job->result ) {
			Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: %s\n", job->name, job->err );
			cm_res.failed++;
			if ( CM_LoadChunkSlot( job->slot, job->name, job->origin ) ) {
				cm_res.preloads++;
			} else {
				CM_SlotFailed( job->slot, job->name );
			}
		} else if ( cap && CM_ChunkBytesUsed() + job->result->bytes > cap ) {
			cm_res.preloadCapSkips++;			// over the cap: left to residency, when a client comes near
			CM_RetireChunk( job->result );
		} else {
			CM_SlotPut( job->slot, job->result );
			cm_res.preloads++;
			CM_LoadPrint()( "CM_LoadChunk: %s at %.0f %.0f %.0f: %d brushes, read %d ms, parse %d ms, decode %d ms, swapped (spawn preload)\n",
				job->name, job->origin[0], job->origin[1], job->origin[2], job->result->cm.numBrushes, job->readMs,
				job->parseMs, job->decodeMs );
		}
		free( job );
	}
}

/*
==================
CM_ChunkResidency

A frame's residency (docs/levelport-collision.md sections 16 to 24): the drivers are the given origins (a server's
active clients, or a remote client's own player: cl_cgame) and cm_drive's points. A manifest chunk is wanted when a
driver is within cm_chunkLoadDist of its placed bounds in x and y: it is queued for the worker (cm_chunkAsync 1) or
loaded in place (0). Finished chunks are swapped into their slots here, and only here; a chunk is freed when every
driver is beyond cm_chunkEvictDist, when cm_chunkMegs is exceeded (least recently wanted first, never one within
reach), or after cm_chunkIdleEvictSec with no driver at all. A chunk the worker fails is loaded once in place; failing
that too it is given up (dead): it stays a solid box and holds no one.
The worker takes the queued chunk a driver reaches first: its bounds distance over the driver's
closing speed toward it, at least cm_chunkPrioSpeed (a standing driver: nearest first, as before); a queued chunk no
driver wants any more (beyond cm_chunkEvictDist) is dropped before it is read. With cm_chunkPrefetchSec a driver also
wants the chunks around where its velocity takes it in that time (0, the default: off). A client's velocity is its
playerstate's (velocities may be NULL); a virtual driver's is its move since the last frame.
==================
*/
void CM_ChunkResidency( const vec3_t *origins, const vec3_t *velocities, int numOrigins )
{
	static cvar_t	*loadDist, *evictDist, *async, *testDelay, *byCentre, *idleSec, *nearDist, *prioSpeed, *prefetch;
	static int		lastCapLog;
	int				&idleSince = cm_idleSince;		// file scope: CM_ClearChunks resets it at a map change
	int				i, k, frameLoads, t0, queued = 0;
	float			dist[MAX_CM_CHUNKS], boundsDist[MAX_CM_CHUNKS], reach[MAX_CM_CHUNKS];
	vec3_t			dpos[MAX_CLIENTS + MAX_CM_DRIVES], dvel[MAX_CLIENTS + MAX_CM_DRIVES];

	if ( !cm_manifestCount || !cmg.numNodes ) {
		CM_DiscardDoneJobs();			// a change to a map without chunks: nothing to swap them into
		return;
	}
	if ( !loadDist ) {
		loadDist = CM_ChunkLoadDistCvar();
		evictDist = Cvar_Get( "cm_chunkEvictDist", "19200", CVAR_ARCHIVE_ND, "A collision chunk is freed once every driver (an active client or cm_drive point) is this many units beyond its bounds" );
		async = CM_ChunkAsyncCvar();
		testDelay = Cvar_Get( "cm_chunkTestDelayMs", "0", CVAR_CHEAT, "Test fault: the worker waits this many ms before each chunk load" );
		byCentre = Cvar_Get( "cm_chunkLoadByCentre", "0", CVAR_CHEAT, "Test fault: chunk distances measured to the tile centre instead of its bounds" );	// a test-only fault (section 23)
		idleSec = Cvar_Get( "cm_chunkIdleEvictSec", "30", CVAR_ARCHIVE_ND, "Seconds with no driver at all before the collision chunks are freed" );
		nearDist = Cvar_Get( "cm_chunkNearDist", "2000", CVAR_ARCHIVE_ND, "cm_chunkreport counts the frames with a still-loading chunk within this many units of a driver" );
		prioSpeed = Cvar_Get( "cm_chunkPrioSpeed", "400", CVAR_ARCHIVE_ND, "The worker loads first the queued chunk a driver reaches soonest: distance over closing speed, at least this many units/s" );
		prefetch = Cvar_Get( "cm_chunkPrefetchSec", "0", CVAR_ARCHIVE_ND, "A driver also loads the chunks its velocity reaches in this many seconds (0 = off)" );
	}
	cm_jobTestDelayMs = testDelay->integer;
	cm_heldNow = qfalse;		// SV_ChunkFrame's hold loop sets it again for this frame's held clients
	for ( i = 0 ; i < cm_manifestCount ; i++ ) {
		cm_keepFrames[i] -= cm_keepFrames[i] > 0 ? 1 : 0;	// a static item's chunk, kept until it has dropped (CM_PreloadStatics)
	}
	if ( !numOrigins && !cm_numDrives ) {
		// no driver: finished loads are not wanted, but the spawn preload's are swapped in (it ran before any client);
		// after cm_chunkIdleEvictSec every unpinned chunk goes (an empty server does not park its last players' set)
		CM_SwapPreloadJobs();
		const int now = Sys_Milliseconds();
		if ( !idleSince ) {
			idleSince = now;
		} else if ( now - idleSince >= idleSec->integer * 1000 ) {
			bool evicted = false;
			for ( i = 0 ; i < cm_manifestCount ; i++ ) {
				if ( cm_chunks[i] && !cm_pinned[i] && !cm_keepFrames[i] ) {
					CM_SlotEvict( i );
					evicted = true;
				}
			}
			if ( evicted ) {
				CM_ListUnloaded();
			}
		}
		return;
	}
	idleSince = 0;
	t0 = Sys_Milliseconds();
	frameLoads = 0;
	cm_res.frames++;
	cm_frameSeq++;
	// the drivers, clients then virtual ones, with their velocities
	numOrigins = numOrigins < MAX_CLIENTS ? numOrigins : MAX_CLIENTS;
	const int	numDrivers = numOrigins + cm_numDrives;
	const float	dt = cm_drivesPrevTime && t0 > cm_drivesPrevTime ? ( t0 - cm_drivesPrevTime ) * 0.001f : 0.0f;
	for ( k = 0 ; k < numOrigins ; k++ ) {
		VectorCopy( origins[k], dpos[k] );
		if ( velocities ) {
			VectorCopy( velocities[k], dvel[k] );
		} else {
			VectorClear( dvel[k] );
		}
	}
	for ( k = 0 ; k < cm_numDrives ; k++ ) {
		vec3_t	v;
		VectorCopy( cm_drives[k], dpos[numOrigins + k] );
		VectorSubtract( cm_drives[k], cm_drivesPrev[k], v );
		VectorScale( v, dt > 0 ? 1.0f / dt : 0.0f, v );
		if ( VectorLengthSquared( v ) > 5000.0f * 5000.0f ) {
			VectorClear( v );			// a placement, not a move
		}
		VectorCopy( v, dvel[numOrigins + k] );
		VectorCopy( cm_drives[k], cm_drivesPrev[k] );
	}
	cm_drivesPrevTime = t0;
	// the cm_chunkMegs cap counts the resident and the transient bytes; a chunk within the load distance still loads
	// over it (putting it off leaves a false wall: r3col smoke B, section 35), the cap evicts beyond reach below
	const long long cap = CM_ChunkCapBytes();
	const long long transient = cm_transientBytes;
	if ( transient > cm_res.peakTransient ) {
		cm_res.peakTransient = transient;
	}
	for ( i = 0 ; i < cm_manifestCount ; i++ ) {
		const cmManifestEntry_t *e = &cm_manifest[i];
		float d = 1e30f, want = 1e30f, r = 1e30f;
		for ( k = 0 ; k < numDrivers ; k++ ) {
			const float dk = CM_BoundsDist2D( dpos[k], e->mins, e->maxs );
			d = dk < d ? dk : d;
			// time to reach: the bounds distance over the closing speed toward the nearest bounds point
			float closing = 0.0f;
			if ( dk > 0 ) {
				const float qx = dpos[k][0] < e->mins[0] ? e->mins[0] : ( dpos[k][0] > e->maxs[0] ? e->maxs[0] : dpos[k][0] );
				const float qy = dpos[k][1] < e->mins[1] ? e->mins[1] : ( dpos[k][1] > e->maxs[1] ? e->maxs[1] : dpos[k][1] );
				closing = ( dvel[k][0] * ( qx - dpos[k][0] ) + dvel[k][1] * ( qy - dpos[k][1] ) ) / dk;
			}
			const float floorSpeed = prioSpeed->value > 1 ? prioSpeed->value : 1;
			const float rk = dk / ( closing > floorSpeed ? closing : floorSpeed );
			r = rk < r ? rk : r;
			if ( prefetch->value > 0 ) {
				vec3_t	ahead;
				VectorMA( dpos[k], prefetch->value, dvel[k], ahead );
				const float da = CM_BoundsDist2D( ahead, e->mins, e->maxs );
				want = da < want ? da : want;
			}
		}
		boundsDist[i] = d;
		reach[i] = r;
		if ( byCentre->integer ) {		// the fault: to the tile's centre (its origin is the tile's min corner)
			vec3_t	c0, c1;
			VectorSet( c0, e->origin[0] + 4800, e->origin[1] + 4800, 0 );
			VectorCopy( c0, c1 );
			d = 1e30f;
			for ( k = 0 ; k < numDrivers ; k++ ) {
				const float dk = CM_BoundsDist2D( dpos[k], c0, c1 );
				d = dk < d ? dk : d;
			}
		}
		d = want < d ? want : d;		// cm_chunkPrefetchSec's point ahead
		dist[i] = d;
		if ( d <= loadDist->value || cm_pinned[i] ) {
			cm_lastWanted[i] = cm_frameSeq;
		}
		if ( !cm_chunks[i] && !cm_slotPending[i] && CM_SlotTryNow( i ) && d <= loadDist->value ) {
			if ( async->integer && CM_QueueChunk( i, e->name, e->origin, reach[i] ) ) {
				queued++;
			} else if ( CM_LoadChunkSlot( i, e->name, e->origin ) ) {
				cm_res.loads++;
				frameLoads++;
			} else {
				CM_SlotFailed( i, e->name );	// read through the file system and still bad: given up, or memory: later
			}
		} else if ( cm_chunks[i] && d > evictDist->value && !cm_pinned[i] && !cm_keepFrames[i] ) {
			CM_SlotEvict( i );
		}
	}
	if ( queued ) {
		CM_FlushChunkJobs();
	}
	// the worker's finished chunks: swapped in when still wanted, else retired; a failed one is loaded once in place
	// through the file system (zip64, a loose override), then given up
	{
		std::vector<cmJob_t *> done = CM_TakeDoneJobs();
		{
			std::lock_guard<std::mutex> lock( cm_jobLock );
			for ( size_t n = 0 ; n < cm_jobQueue.size() ; ) {
				cmJob_t *job = cm_jobQueue[n];
				if ( job->generation == cm_jobGeneration && dist[job->slot] > evictDist->value && !cm_pinned[job->slot] ) {
					cm_slotPending[job->slot] = 0;		// no driver wants it: dropped before its read (M-5)
					cm_jobQueue.erase( cm_jobQueue.begin() + n );
					free( job );
					cm_res.cancelled++;
					continue;
				}
				job->dist = reach[job->slot];		// the queue's order follows the drivers: who reaches it first
				n++;
			}
		}
		for ( cmJob_t *job : done ) {
			const bool current = job->generation == cm_jobGeneration;
			CM_JobLanded( job );
			if ( !job->result ) {
				Com_Printf( S_COLOR_YELLOW "CM_LoadChunk: %s: %s\n", job->name, job->err );
				cm_res.failed++;
				if ( current && !cm_chunks[job->slot] && dist[job->slot] <= evictDist->value ) {
					if ( CM_LoadChunkSlot( job->slot, job->name, job->origin ) ) {
						cm_res.loads++;
						frameLoads++;
					} else {
						CM_SlotFailed( job->slot, job->name );
					}
				}
			} else if ( !current || cm_chunks[job->slot] || dist[job->slot] > evictDist->value ) {
				CM_RetireChunk( job->result );		// a map change, or no driver wants it any more
			} else {
				cmChunk_t *c = job->result;
				CM_SlotPut( job->slot, c );			// the swap: from here the traces see it
				cm_res.loads++;
				frameLoads++;
				cm_res.maxReadMs = job->readMs > cm_res.maxReadMs ? job->readMs : cm_res.maxReadMs;
				cm_res.maxParseMs = job->parseMs > cm_res.maxParseMs ? job->parseMs : cm_res.maxParseMs;
				cm_res.sumReadMs += job->readMs;
				cm_res.sumParseMs += job->parseMs;
				cm_res.maxDecodeMs = job->decodeMs > cm_res.maxDecodeMs ? job->decodeMs : cm_res.maxDecodeMs;
				cm_res.sumDecodeMs += job->decodeMs;
				CM_LoadPrint()( "CM_LoadChunk: %s at %.0f %.0f %.0f: %d brushes, read %d ms, parse %d ms, decode %d ms, swapped\n",
					job->name, job->origin[0], job->origin[1], job->origin[2], c->cm.numBrushes, job->readMs,
					job->parseMs, job->decodeMs );
			}
			free( job );
		}
	}
	// cm_chunkMegs: over the cap, the least recently wanted chunks beyond reach go first; never one within reach. The
	// transient bytes count; a chunk evicted here is transient until the worker frees it, so the resident bytes are
	// brought under the cap less the transient bytes this frame began with
	const long long residentCap = cap - transient;
	if ( cap && cm_res.bytes > residentCap ) {
		std::vector<int> order;
		for ( i = 0 ; i < cm_manifestCount ; i++ ) {
			if ( cm_chunks[i] && !cm_pinned[i] && !cm_keepFrames[i] && dist[i] > loadDist->value ) {
				order.push_back( i );
			}
		}
		std::sort( order.begin(), order.end(), []( int a, int b ) { return cm_lastWanted[a] < cm_lastWanted[b]; } );
		for ( size_t n = 0 ; n < order.size() && cm_res.bytes > residentCap ; n++ ) {
			CM_SlotEvict( order[n] );
			cm_res.capEvictions++;
		}
		if ( cm_res.bytes > residentCap ) {
			cm_res.capMisses++;
			if ( t0 - lastCapLog > 10000 ) {
				lastCapLog = t0;
				Com_Printf( S_COLOR_YELLOW "cm_chunkMegs cap %lld MB: %lld MB of chunks are within reach of the players "
					"(%lld MB transient)\n", cap >> 20, cm_res.bytes >> 20, transient >> 20 );
			}
		}
	}
	// the false-wall invariant (section 23): no chunk within the load distance of a driver is neither resident nor
	// loading; a loading one nearer than cm_chunkNearDist is counted apart (its box is a wall until it lands), and
	// the nearest a non-resident and a loading box came are reported
	{
		bool	falseWall = false, nearPending = false;
		for ( i = 0 ; i < cm_manifestCount ; i++ ) {
			if ( cm_chunks[i] || cm_slotDead[i] ) {
				continue;
			}
			if ( boundsDist[i] < cm_res.nearestUnloaded ) {
				cm_res.nearestUnloaded = boundsDist[i];
			}
			if ( cm_slotPending[i] ) {
				if ( boundsDist[i] < cm_res.nearestPending ) {
					cm_res.nearestPending = boundsDist[i];
				}
				if ( boundsDist[i] <= nearDist->value ) {
					nearPending = true;
				}
			} else if ( boundsDist[i] <= loadDist->value ) {
				falseWall = true;
			}
		}
		if ( falseWall ) {
			cm_res.falseWallFrames++;
		}
		if ( nearPending ) {
			cm_res.nearPendingFrames++;
		}
	}
	// the invariant and the hold, for the virtual drivers (clients are held in sv_client by the same test)
	for ( k = 0 ; k < cm_numDrives ; k++ ) {
		if ( !CM_ChunkReadyAt( cm_drives[k] ) ) {
			cm_res.violations++;
			cm_res.heldFrames++;
			if ( !cm_res.firstHeld ) {
				cm_res.firstHeld = cm_res.frames;
			}
			cm_res.lastHeld = cm_res.frames;
		}
	}
	CM_ListUnloaded();
	const int ms = Sys_Milliseconds() - t0;
	if ( ms > cm_res.worstFrameMs ) {
		cm_res.worstFrameMs = ms;
		cm_res.worstFrameLoads = frameLoads;
	}
}
