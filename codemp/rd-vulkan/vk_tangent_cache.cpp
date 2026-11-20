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
Prepared tangents (docs/character-loading-and-picker.md, milestone 1).

R_BuildMDXM and the hair substitute buffers (vk_hair.cpp) both pack every LOD of a GLM with its MikkTSpace tangents.
The tangents depend only on the GLM's bytes, yet a hero spent ~0.47 s on them per load, twice (once per consumer).
R_MdxmTangents keeps them for this map's models, reads them from cache/tangents/<key>.xtan (homepath or a pk3; the key
is a hash of the bytes the tangents come from, so a short-name copy of a model shares its file), and computes them
once on a miss and writes the file. Vertices no triangle uses get zero tangents (R_BuildMDXM's temp buffer left them
unset). tools/characters/cook_tangents.py builds this file's key, computation and layout outside the game to prepare
the files ahead of play.

Version 2: the key covers each LOD's surfaces (vertex and triangle counts, triangles, vertices, texture
coordinates) instead of the whole loaded GLM, whose header and surface hierarchy hold this session's skeleton and
shader handles (R_LoadMDXM writes mdxm->animIndex and every surface's shaderIndex before R_BuildMDXM): version 1's
key changed with the map and the load order, so a cached file was found only when they repeated.

r_tangentCache: 0 = every consumer computes its own (the old path), 1 = cache (default), 2 = cache and verify: compute
as well on every cache read and compare byte for byte.
*/
#include "tr_local.h"

#include <string.h>
#include <vector>

#define XTAN_MAGIC		0x4e415458	// "XTAN"
#define XTAN_VERSION	2			// bump when vk_mikkt_mdxm_generate, the key or this layout changes
#define TANGENT_ENTRIES	6			// models kept in memory (a hero's four LODs are ~4 MB)

typedef struct {
	uint32_t	magic;
	uint32_t	version;
	uint64_t	key;
	uint32_t	glmSize;
	uint32_t	numLODs;
	// numLODs vertex counts (uint32_t), then each LOD's tangents (4 floats a vertex)
} xtanHeader_t;

struct tangentEntry_t {
	const mdxmHeader_t			*mdxm;		// the copy last asked for, for the lookup without hashing
	uint32_t					size;
	uint64_t					key;
	std::vector<int>			numVerts;
	std::vector<std::vector<float> >	lods;
	int							lastUse;
};

static std::vector<tangentEntry_t>	s_tangents;
static int							s_tangentUse;

void R_TangentCacheClear( void ) {
	s_tangents.clear();
}

// the model cache may free GLMs (map change, devmapmdl, sv_pure); another GLM of the same size could then
// sit at a freed one's address and take its tangents through the pointer fast path. Entries stay (the content hash
// finds them again and records the new pointer); only the pointers are forgotten
void R_TangentCacheForgetPointers( void ) {
	for ( size_t i = 0; i < s_tangents.size(); i++ ) s_tangents[i].mdxm = NULL;
}

static uint64_t R_TangentMix( uint64_t h, const byte *p, size_t n ) {
	size_t i = 0;
	for ( ; i + 8 <= n; i += 8 ) {
		uint64_t w;
		memcpy( &w, p + i, 8 );
		h = ( h ^ w ) * 0xff51afd7ed558ccdULL;
		h ^= h >> 29;
	}
	for ( ; i < n; i++ ) {
		h = ( h ^ p[i] ) * 0xff51afd7ed558ccdULL;
		h ^= h >> 29;
	}
	return h;
}

static const mdxmSurface_t *R_TangentFirstSurface( const mdxmHeader_t *mdxm, const mdxmLOD_t *lod ) {
	return (const mdxmSurface_t *)( (const byte *)lod + sizeof( mdxmLOD_t ) + mdxm->numSurfaces * sizeof( mdxmLODSurfOffset_t ) );
}

// every LOD's vertex count, walked as R_BuildMDXM walks them
static void R_TangentCounts( const mdxmHeader_t *mdxm, std::vector<int> &out ) {
	const mdxmLOD_t *lod = (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs );
	out.assign( mdxm->numLODs, 0 );
	for ( int l = 0; l < mdxm->numLODs; l++ ) {
		const mdxmSurface_t *surf = R_TangentFirstSurface( mdxm, lod );
		for ( int n = 0; n < mdxm->numSurfaces; n++ ) {
			out[l] += surf->numVerts;
			surf = (const mdxmSurface_t *)( (const byte *)surf + surf->ofsEnd );
		}
		lod = (const mdxmLOD_t *)( (const byte *)lod + lod->ofsEnd );
	}
}

// the tangents exactly as R_BuildMDXM computed them: per surface, its own triangle indices (any thread)
void R_TangentComputeLods( const mdxmHeader_t *mdxm, std::vector<std::vector<float> > &lods ) {
	std::vector<int> counts;
	std::vector<glIndex_t> idx;
	R_TangentCounts( mdxm, counts );
	lods.assign( mdxm->numLODs, std::vector<float>() );
	const mdxmLOD_t *lod = (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs );
	for ( int l = 0; l < mdxm->numLODs; l++ ) {
		lods[l].assign( (size_t)counts[l] * 4, 0.0f );
		const mdxmSurface_t *surf = R_TangentFirstSurface( mdxm, lod );
		int base = 0;
		for ( int n = 0; n < mdxm->numSurfaces; n++ ) {
			if ( surf->numTriangles > 0 ) {
				const mdxmTriangle_t *t = (const mdxmTriangle_t *)( (const byte *)surf + surf->ofsTriangles );
				mdxmVertex_t *v = (mdxmVertex_t *)( (byte *)surf + surf->ofsVerts );
				mdxmVertexTexCoord_t *tc = (mdxmVertexTexCoord_t *)( v + surf->numVerts );
				idx.resize( (size_t)surf->numTriangles * 3 );
				for ( int k = 0; k < surf->numTriangles; k++ ) {
					idx[k * 3 + 0] = t[k].indexes[0];
					idx[k * 3 + 1] = t[k].indexes[1];
					idx[k * 3 + 2] = t[k].indexes[2];
				}
				vk_mikkt_mdxm_generate( surf->numTriangles, v, tc, (vec4_t *)&lods[l][(size_t)base * 4], &idx[0] );
			}
			base += surf->numVerts;
			surf = (const mdxmSurface_t *)( (const byte *)surf + surf->ofsEnd );
		}
		lod = (const mdxmLOD_t *)( (const byte *)lod + lod->ofsEnd );
	}
}

static void R_TangentCompute( const mdxmHeader_t *mdxm, std::vector<std::vector<float> > &lods ) {
	loadProfScope_t prof( LOADPROF_MIKKT );
	R_TangentComputeLods( mdxm, lods );
}

bool R_TangentCheckSurface( const mdxmSurface_t *surf, const float *prepared ) {
	std::vector<float> own( (size_t)surf->numVerts * 4 + 4, 0.0f );
	std::vector<glIndex_t> idx( (size_t)surf->numTriangles * 3 + 1 );
	const mdxmTriangle_t *t = (const mdxmTriangle_t *)( (const byte *)surf + surf->ofsTriangles );
	mdxmVertex_t *v = (mdxmVertex_t *)( (byte *)surf + surf->ofsVerts );
	for ( int k = 0; k < surf->numTriangles * 3; k++ ) idx[k] = t[k / 3].indexes[k % 3];
	if ( surf->numTriangles > 0 ) vk_mikkt_mdxm_generate( surf->numTriangles, v, (mdxmVertexTexCoord_t *)( v + surf->numVerts ), (vec4_t *)&own[0], &idx[0] );
	return !surf->numVerts || !memcmp( &own[0], prepared, (size_t)surf->numVerts * 4 * sizeof( float ) );
}

static const char *R_TangentPath( uint64_t key ) {
	return va( "cache/tangents/%08x%08x.xtan", (unsigned)( key >> 32 ), (unsigned)key );
}

static bool R_TangentRead( tangentEntry_t &e ) {
	void *buf = NULL;
	long len = ri.FS_ReadFile( R_TangentPath( e.key ), &buf );
	if ( ( len <= 0 || !buf ) && ri.FS_ReadHomeFile ) {	// a pure server refuses loose files: the one this cache wrote
		if ( buf ) ri.FS_FreeFile( buf );
		buf = NULL;
		len = ri.FS_ReadHomeFile( R_TangentPath( e.key ), &buf );
	}
	if ( len <= 0 || !buf ) {
		if ( buf ) ri.FS_FreeFile( buf );
		return false;
	}
	bool ok = false;
	const xtanHeader_t *h = (const xtanHeader_t *)buf;
	const size_t countsAt = sizeof( xtanHeader_t );
	if ( (size_t)len >= countsAt && h->magic == XTAN_MAGIC && h->version == XTAN_VERSION && h->key == e.key
		&& h->glmSize == e.size && h->numLODs == e.numVerts.size()
		&& (size_t)len >= countsAt + h->numLODs * sizeof( uint32_t ) ) {
		const uint32_t *counts = (const uint32_t *)( (const byte *)buf + countsAt );
		size_t need = countsAt + h->numLODs * sizeof( uint32_t );
		ok = true;
		for ( uint32_t l = 0; l < h->numLODs; l++ ) {
			ok = ok && (int)counts[l] == e.numVerts[l];
			need += (size_t)counts[l] * 4 * sizeof( float );
		}
		ok = ok && (size_t)len == need;
		if ( ok ) {
			const float *p = (const float *)( counts + h->numLODs );
			e.lods.assign( h->numLODs, std::vector<float>() );
			for ( uint32_t l = 0; l < h->numLODs; l++ ) {
				e.lods[l].assign( p, p + (size_t)counts[l] * 4 );
				p += (size_t)counts[l] * 4;
			}
		}
	}
	ri.FS_FreeFile( buf );
	return ok;
}

// a .xtan file's bytes (also built outside the game: cook_tangents.py)
void R_TangentSerialize( uint64_t key, uint32_t glmSize, const std::vector<int> &numVerts,
	const std::vector<std::vector<float> > &lods, std::vector<byte> &out ) {
	xtanHeader_t h;
	h.magic = XTAN_MAGIC;
	h.version = XTAN_VERSION;
	h.key = key;
	h.glmSize = glmSize;
	h.numLODs = (uint32_t)lods.size();
	out.assign( sizeof( h ), 0 );
	memcpy( &out[0], &h, sizeof( h ) );
	for ( size_t l = 0; l < lods.size(); l++ ) {
		const uint32_t n = (uint32_t)numVerts[l];
		out.insert( out.end(), (const byte *)&n, (const byte *)&n + sizeof( n ) );
	}
	for ( size_t l = 0; l < lods.size(); l++ ) {
		if ( !lods[l].empty() ) {
			out.insert( out.end(), (const byte *)&lods[l][0], (const byte *)&lods[l][0] + lods[l].size() * sizeof( float ) );
		}
	}
}

static void R_TangentWrite( const tangentEntry_t &e ) {
	std::vector<byte> out;
	R_TangentSerialize( e.key, e.size, e.numVerts, e.lods, out );
	ri.FS_WriteFile( R_TangentPath( e.key ), &out[0], (int)out.size() );
}

// r_tangentCache 2: the cached tangents against a fresh computation
static void R_TangentVerify( const mdxmHeader_t *mdxm, const tangentEntry_t &e ) {
	std::vector<std::vector<float> > fresh;
	R_TangentCompute( mdxm, fresh );
	int bad = 0;
	for ( size_t l = 0; l < e.lods.size(); l++ ) {
		if ( fresh[l].size() != e.lods[l].size()
			|| ( !fresh[l].empty() && memcmp( &fresh[l][0], &e.lods[l][0], fresh[l].size() * sizeof( float ) ) ) ) {
			bad++;
		}
	}
	ri.Printf( PRINT_ALL, "tangent cache: %s: %s, %d of %d LODs differ\n", mdxm->name, R_TangentPath( e.key ), bad, (int)e.lods.size() );
}

// the bytes R_TangentComputeLods reads, as the file has them (nothing R_LoadMDXM writes)
uint64_t R_TangentKeyOf( const mdxmHeader_t *mdxm ) {
	const int32_t shape[2] = { mdxm->numLODs, mdxm->numSurfaces };
	uint64_t h = 0x9E3779B97F4A7C15ULL ^ (uint64_t)(uint32_t)mdxm->ofsEnd ^ ( (uint64_t)XTAN_VERSION << 56 );
	h = R_TangentMix( h, (const byte *)shape, sizeof( shape ) );
	const mdxmLOD_t *lod = (const mdxmLOD_t *)( (const byte *)mdxm + mdxm->ofsLODs );
	for ( int l = 0; l < mdxm->numLODs; l++ ) {
		const mdxmSurface_t *surf = R_TangentFirstSurface( mdxm, lod );
		for ( int n = 0; n < mdxm->numSurfaces; n++ ) {
			const int32_t counts[2] = { surf->numVerts, surf->numTriangles };
			h = R_TangentMix( h, (const byte *)counts, sizeof( counts ) );
			if ( surf->numTriangles > 0 ) {
				h = R_TangentMix( h, (const byte *)surf + surf->ofsTriangles, (size_t)surf->numTriangles * sizeof( mdxmTriangle_t ) );
			}
			if ( surf->numVerts > 0 ) {
				h = R_TangentMix( h, (const byte *)surf + surf->ofsVerts,
					(size_t)surf->numVerts * ( sizeof( mdxmVertex_t ) + sizeof( mdxmVertexTexCoord_t ) ) );	// texcoords follow the vertices
			}
			surf = (const mdxmSurface_t *)( (const byte *)surf + surf->ofsEnd );
		}
		lod = (const mdxmLOD_t *)( (const byte *)lod + lod->ofsEnd );
	}
	return h ^ ( h >> 32 );
}

static tangentEntry_t *R_TangentFindKey( uint64_t key, uint32_t size ) {
	for ( size_t i = 0; i < s_tangents.size(); i++ ) {
		if ( s_tangents[i].key == key && s_tangents[i].size == size ) return &s_tangents[i];
	}
	return NULL;
}

// a new entry, the least recently used one dropped when the cache is full
static tangentEntry_t *R_TangentNewEntry( const mdxmHeader_t *mdxm, uint64_t key, uint32_t size ) {
	if ( (int)s_tangents.size() >= TANGENT_ENTRIES ) {
		size_t oldest = 0;
		for ( size_t i = 1; i < s_tangents.size(); i++ ) {
			if ( s_tangents[i].lastUse < s_tangents[oldest].lastUse ) oldest = i;
		}
		s_tangents.erase( s_tangents.begin() + oldest );
	}
	s_tangents.push_back( tangentEntry_t() );
	tangentEntry_t *e = &s_tangents.back();
	e->key = key;
	e->size = size;
	e->mdxm = NULL;
	e->lastUse = 0;
	R_TangentCounts( mdxm, e->numVerts );
	return e;
}

bool R_TangentFetch( const mdxmHeader_t *mdxm, uint64_t key, std::vector<std::vector<float> > &lods ) {
	if ( !r_tangentCache || !r_tangentCache->integer || !mdxm ) {
		return false;
	}
	loadProfScope_t prof( LOADPROF_TANGENTS );
	const uint32_t size = (uint32_t)mdxm->ofsEnd;
	tangentEntry_t *e = R_TangentFindKey( key, size );
	if ( !e ) {
		e = R_TangentNewEntry( mdxm, key, size );
		if ( !R_TangentRead( *e ) ) {
			s_tangents.pop_back();
			return false;
		}
		ri.Printf( PRINT_DEVELOPER, "tangent cache: %s: read %s\n", mdxm->name, R_TangentPath( key ) );
	}
	e->mdxm = mdxm;
	e->lastUse = ++s_tangentUse;
	lods = e->lods;
	return true;
}

void R_TangentAdopt( const mdxmHeader_t *mdxm, uint64_t key, const std::vector<std::vector<float> > &lods ) {
	if ( !r_tangentCache || !r_tangentCache->integer || !mdxm ) {
		return;
	}
	const uint32_t size = (uint32_t)mdxm->ofsEnd;
	tangentEntry_t *e = R_TangentFindKey( key, size );
	if ( !e ) {
		e = R_TangentNewEntry( mdxm, key, size );
		e->lods = lods;
		R_TangentWrite( *e );
		ri.Printf( PRINT_DEVELOPER, "tangent cache: %s: computed on the mesh worker, wrote %s\n", mdxm->name, R_TangentPath( key ) );
	}
	e->mdxm = mdxm;
	e->lastUse = ++s_tangentUse;
}

const float *R_MdxmTangents( const mdxmHeader_t *mdxm, int lodIndex, int numVerts ) {
	const int mode = r_tangentCache ? r_tangentCache->integer : 0;
	if ( !mode || !mdxm || lodIndex < 0 || lodIndex >= mdxm->numLODs ) {
		return NULL;
	}
	loadProfScope_t prof( LOADPROF_TANGENTS );
	const uint32_t size = (uint32_t)mdxm->ofsEnd;
	tangentEntry_t *e = NULL;
	for ( size_t i = 0; i < s_tangents.size() && !e; i++ ) {
		if ( s_tangents[i].mdxm == mdxm && s_tangents[i].size == size ) e = &s_tangents[i];
	}
	if ( !e ) {
		const uint64_t key = R_TangentKeyOf( mdxm );
		for ( size_t i = 0; i < s_tangents.size() && !e; i++ ) {
			if ( s_tangents[i].key == key && s_tangents[i].size == size ) e = &s_tangents[i];
		}
		if ( !e ) {
			e = R_TangentNewEntry( mdxm, key, size );
			if ( R_TangentRead( *e ) ) {
				ri.Printf( PRINT_DEVELOPER, "tangent cache: %s: read %s\n", mdxm->name, R_TangentPath( key ) );
				if ( mode == 2 ) R_TangentVerify( mdxm, *e );
			} else {
				R_TangentCompute( mdxm, e->lods );
				R_TangentWrite( *e );
				ri.Printf( PRINT_DEVELOPER, "tangent cache: %s: computed, wrote %s\n", mdxm->name, R_TangentPath( key ) );
			}
		}
		e->mdxm = mdxm;
	}
	e->lastUse = ++s_tangentUse;
	if ( e->numVerts[lodIndex] != numVerts ) {
		return NULL;   // the caller's walk disagrees: let it compute its own
	}
	return e->lods[lodIndex].empty() ? NULL : &e->lods[lodIndex][0];
}
