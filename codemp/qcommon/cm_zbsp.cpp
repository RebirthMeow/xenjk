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
===========================================================================
xenjk collision option C: a chunk BSP stored as XBSZ (tools/levelport/colzbsp.py), each lump one zstd
frame (the integer lumps byte-shuffled first), decoded here to the exact original RBSP bytes before CM_ParseChunk
reads them. Each frame's content checksum is zstd's own check.

Memory: the decoded file and, for a shuffled lump, zstd's window (at most 8 MB: level 19's windowLog 23, no
long-range mode) and one small output block: the shuffled bytes stream out of the decoder and go straight to their
records' places, so no lump-sized scratch is held. A plain lump decodes in one shot into its place.

zstd: the engine's CMake build defines XENJK_ZSTD_LINKED and links lib/zstd's decoder (bundled_zstd), whose functions
are called directly. Built without it, zstd is loaded at run time (libzstd.dll beside the executable, libzstd.so.1
elsewhere), and a binary that has neither reads plain chunk BSPs as before.
===========================================================================
*/

#include "cm_zbsp.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef XENJK_ZSTD_LINKED

#include "zstd.h"
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace {

// the parts of zstd's API used here (zstd.h's names and types)
struct ZInBuffer { const void *src; size_t size; size_t pos; };
struct ZOutBuffer { void *dst; size_t size; size_t pos; };

struct ZstdApi {
	size_t				( *decompress )( void *dst, size_t dstCapacity, const void *src, size_t srcSize );
	unsigned long long	( *contentSize )( const void *src, size_t srcSize );
	unsigned			( *isError )( size_t code );
	const char			*( *errorName )( size_t code );
	void				*( *createDStream )( void );
	size_t				( *freeDStream )( void *zds );
	size_t				( *initDStream )( void *zds );
	size_t				( *decompressStream )( void *zds, ZOutBuffer *output, ZInBuffer *input );
	bool				tried, ok;
};

ZstdApi		zstdApi;
std::mutex	zstdLock;

bool ZstdLoad( char *err, int errLen ) {
	std::lock_guard<std::mutex> lock( zstdLock );
	if ( !zstdApi.tried ) {
		zstdApi.tried = true;
#ifdef XENJK_ZSTD_LINKED
		static_assert( sizeof( ZInBuffer ) == sizeof( ZSTD_inBuffer ) && sizeof( ZOutBuffer ) == sizeof( ZSTD_outBuffer ), "zstd buffers" );
		zstdApi.decompress = ZSTD_decompress;
		zstdApi.contentSize = ZSTD_getFrameContentSize;
		zstdApi.isError = ZSTD_isError;
		zstdApi.errorName = ZSTD_getErrorName;
		zstdApi.createDStream = []() -> void * { return ZSTD_createDStream(); };
		zstdApi.freeDStream = []( void *z ) -> size_t { return ZSTD_freeDStream( (ZSTD_DStream *)z ); };
		zstdApi.initDStream = []( void *z ) -> size_t { return ZSTD_initDStream( (ZSTD_DStream *)z ); };
		zstdApi.decompressStream = []( void *z, ZOutBuffer *o, ZInBuffer *i ) -> size_t {
			return ZSTD_decompressStream( (ZSTD_DStream *)z, (ZSTD_outBuffer *)o, (ZSTD_inBuffer *)i );
		};
		zstdApi.ok = true;
#else
#ifdef _WIN32
		HMODULE h = LoadLibraryA( "libzstd.dll" );
		#define ZSYM( n ) GetProcAddress( h, n )
#else
		void *h = dlopen( "libzstd.so.1", RTLD_NOW | RTLD_LOCAL );
		#define ZSYM( n ) dlsym( h, n )
#endif
		if ( h ) {
			*(void **)&zstdApi.decompress = (void *)ZSYM( "ZSTD_decompress" );
			*(void **)&zstdApi.contentSize = (void *)ZSYM( "ZSTD_getFrameContentSize" );
			*(void **)&zstdApi.isError = (void *)ZSYM( "ZSTD_isError" );
			*(void **)&zstdApi.errorName = (void *)ZSYM( "ZSTD_getErrorName" );
			*(void **)&zstdApi.createDStream = (void *)ZSYM( "ZSTD_createDStream" );
			*(void **)&zstdApi.freeDStream = (void *)ZSYM( "ZSTD_freeDStream" );
			*(void **)&zstdApi.initDStream = (void *)ZSYM( "ZSTD_initDStream" );
			*(void **)&zstdApi.decompressStream = (void *)ZSYM( "ZSTD_decompressStream" );
			zstdApi.ok = zstdApi.decompress && zstdApi.contentSize && zstdApi.isError && zstdApi.errorName
			          && zstdApi.createDStream && zstdApi.freeDStream && zstdApi.initDStream && zstdApi.decompressStream;
		}
		#undef ZSYM
#endif
	}
	if ( !zstdApi.ok ) {
		snprintf( err, errLen, "XBSZ chunk: no zstd decoder" );
	}
	return zstdApi.ok;
}

inline uint32_t Get32( const unsigned char *p ) {
	return (uint32_t)p[0] | ( (uint32_t)p[1] << 8 ) | ( (uint32_t)p[2] << 16 ) | ( (uint32_t)p[3] << 24 );
}

const int XBSZ_LUMPS = 18;
const int XBSZ_HEAD = 8 + 8 * XBSZ_LUMPS;		// the RBSP header it carries
const size_t STREAM_BLOCK = 1 << 17;			// the shuffled bytes' output block (zstd's own block size)

// a shuffled lump: the frame decoded in blocks, each byte put at its record's place (byte k of record j was at
// k * count + j); false with err filled
bool DecodeShuffled( const unsigned char *frame, size_t m, unsigned char *out, uint32_t n, uint32_t rec,
		unsigned char *block, int lump, char *err, int errLen ) {
	void *zds = zstdApi.createDStream();
	if ( !zds ) {
		snprintf( err, errLen, "out of memory decoding" );
		return false;
	}
	const uint32_t count = n / rec;
	size_t q = 0;							// shuffled bytes out so far
	bool ok = !zstdApi.isError( zstdApi.initDStream( zds ) );
	ZInBuffer in = { frame, m, 0 };
	while ( ok ) {
		ZOutBuffer o = { block, STREAM_BLOCK, 0 };
		const size_t r = zstdApi.decompressStream( zds, &o, &in );
		if ( zstdApi.isError( r ) ) {
			snprintf( err, errLen, "XBSZ chunk: lump %d: %s", lump, zstdApi.errorName( r ) );
			ok = false;
			break;
		}
		if ( q + o.pos > n ) {
			snprintf( err, errLen, "XBSZ chunk: lump %d decodes past %u bytes", lump, n );
			ok = false;
			break;
		}
		for ( size_t i = 0 ; i < o.pos ; ) {	// a run of one byte position k, records j .. j + run
			const uint32_t k = (uint32_t)( ( q + i ) / count ), j = (uint32_t)( ( q + i ) % count );
			const size_t run = ( count - j ) < ( o.pos - i ) ? ( count - j ) : ( o.pos - i );
			unsigned char *d = out + (size_t)j * rec + k;
			const unsigned char *s = block + i;
			for ( size_t t = 0 ; t < run ; t++ ) {
				d[t * rec] = s[t];
			}
			i += run;
		}
		q += o.pos;
		if ( r == 0 ) {						// the frame is done (its checksum checked)
			break;
		}
		if ( in.pos == in.size && o.pos < o.size ) {
			snprintf( err, errLen, "XBSZ chunk: lump %d's frame ends early", lump );
			ok = false;
			break;
		}
	}
	zstdApi.freeDStream( zds );
	if ( ok && q != n ) {
		snprintf( err, errLen, "XBSZ chunk: lump %d is %zu bytes, not %u", lump, q, n );
		ok = false;
	}
	return ok;
}

}

bool CM_IsXbsz( const unsigned char *buf, int len ) {
	return buf && len >= 12 && !memcmp( buf, "XBSZ", 4 );
}

unsigned char *CM_XbszDecode( const unsigned char *buf, int len, int *outLen, char *err, int errLen ) {
	*outLen = 0;
	if ( !CM_IsXbsz( buf, len ) || len < 12 + XBSZ_HEAD ) {
		snprintf( err, errLen, "XBSZ chunk: truncated header" );
		return NULL;
	}
	if ( Get32( buf + 4 ) != 1 ) {
		snprintf( err, errLen, "XBSZ chunk: version %u", Get32( buf + 4 ) );
		return NULL;
	}
	if ( !ZstdLoad( err, errLen ) ) {
		return NULL;
	}
	const uint32_t size = Get32( buf + 8 );
	const unsigned char *head = buf + 12;
	if ( size < (uint32_t)XBSZ_HEAD || size > 0x7fffffffu ) {
		snprintf( err, errLen, "XBSZ chunk: size %u", size );
		return NULL;
	}
	unsigned char *out = (unsigned char *)calloc( 1, size );
	unsigned char *block = (unsigned char *)malloc( STREAM_BLOCK );
	if ( !out || !block ) {
		free( out );
		free( block );
		snprintf( err, errLen, "out of memory decoding" );
		return NULL;
	}
	memcpy( out, head, XBSZ_HEAD );
	size_t p = 12 + XBSZ_HEAD;
	for ( int i = 0 ; i < XBSZ_LUMPS ; i++ ) {
		const uint32_t ofs = Get32( head + 8 + 8 * i ), n = Get32( head + 12 + 8 * i );
		if ( p + 8 > (size_t)len ) {
			snprintf( err, errLen, "XBSZ chunk: truncated at lump %d", i );
			goto fail;
		}
		{
			const uint32_t rec = Get32( buf + p ), m = Get32( buf + p + 4 );
			p += 8;
			if ( m > (size_t)len - p || (uint64_t)ofs + n > size || ( n && !m ) || ( rec && n % rec ) ) {
				snprintf( err, errLen, "XBSZ chunk: lump %d out of bounds", i );
				goto fail;
			}
			if ( n ) {
				if ( zstdApi.contentSize( buf + p, m ) != n ) {
					snprintf( err, errLen, "XBSZ chunk: lump %d's frame is not %u bytes", i, n );
					goto fail;
				}
				if ( rec ) {
					if ( !DecodeShuffled( buf + p, m, out + ofs, n, rec, block, i, err, errLen ) ) {
						goto fail;
					}
				} else {
					const size_t r = zstdApi.decompress( out + ofs, n, buf + p, m );
					if ( zstdApi.isError( r ) || r != n ) {
						snprintf( err, errLen, "XBSZ chunk: lump %d: %s", i, zstdApi.isError( r ) ? zstdApi.errorName( r ) : "short" );
						goto fail;
					}
				}
			}
			p += m;
		}
	}
	free( block );
	*outLen = (int)size;
	return out;
fail:
	free( block );
	free( out );
	return NULL;
}
