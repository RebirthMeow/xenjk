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
vk_scene_ple.cpp: FF7 Rebirth's baked probe GI (its PrecomputedLightEnvironment plugin) for a render scene:
maps/<map>.xple (tools/levelport/ff7rple.py pack). scene.frag looks a surface's point up in the probes' sparse grid and
lights its ambient from the 8 corner probes there, as the game's SampleGI pass does, in place of the flat hemisphere.

The file, little endian: "XPLE", version 1, environments; the scene's origin (cm, Quake axes) and units per cm; the
source's sun (lux); then per environment its position (cm, UE axes), grid size x y z, hierarchy log2, the local-to-grid
matrix (16 floats, row-vector rows), the counts (probes, bounds, index entries, occluder triangles, occluder ranges),
and the arrays: probe positions (4 floats), coefficients (9 floats), bounds (12 words), the index grid, the occluder
triangles (9 floats) and ranges.

The GPU buffer (binding 30), 32-bit words: [0] environments, [1..3] origin, [4] units per cm, [5] sun lux; from word 8,
SCENE_PLE_ENV_WORDS a environment: position (0..2), grid size (4..6), hierarchy (7), matrix (8..23), the word offsets of
its probe positions (24), coefficients (25), bounds (26), index (27), occluder triangles (28), occluder ranges (29),
its bound count (30); then the arrays.
*/
#include "vk_scene_local.h"

#define SCENE_PLE_MAX_ENVS	4
#define SCENE_PLE_ENV_WORDS	32

float scenePleSunLux = 0.0f;
int scenePleRefl = -1;
image_t *scenePleReflImage = NULL;

// maps/<map>_refl.dds (tools/levelport/ff7renv.py --reflection): the area's baked reflection capture, its BC6H cube
// faces in a 4 x 2 atlas, in the game's light units as the probes; an image of the GPU-driven array (its index: -1 none)
static void Scene_LoadPleReflection( const char *scnName )
{
	char name[MAX_QPATH];
	qboolean found;

	scenePleRefl = -1;
	scenePleReflImage = NULL;
	if ( scenePleSunLux <= 0.0f || !vk.gpuDriven )
		return;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), "_refl" );
	image_t *image = Scene_Image( name, &found );
	if ( !found || image->internalFormat != VK_FORMAT_BC6H_UFLOAT_BLOCK )
		return;
	for ( size_t k = 0; k < sceneSlotImages.size() && scenePleRefl < 0; k++ )
		if ( sceneSlotImages[k] == image )
			scenePleRefl = (int)k;
	if ( scenePleRefl < 0 ) {
		sceneSlotImages.push_back( image );
		scenePleRefl = (int)sceneSlotImages.size() - 1;
	}
	scenePleReflImage = image;
	ri.Printf( PRINT_ALL, "scene probe GI reflection %s: %i x %i, %i levels\n", name, image->uploadWidth,
		image->uploadHeight, image->mipLevels );
}

void Scene_LoadPle( const char *scnName )
{
	static const uint32_t zero[4] = { 0, 0, 0, 0 };
	char name[MAX_QPATH];
	void *buf = NULL;
	std::vector<uint32_t> words;

	scenePleSunLux = 0.0f;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xple" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len > 32 && buf && !memcmp( buf, "XPLE", 4 ) ) {
		const byte *p = (const byte *)buf, *end = p + len;
		uint32_t version, envs;
		memcpy( &version, p + 4, 4 );
		memcpy( &envs, p + 8, 4 );
		if ( version != 1 || envs == 0 || envs > SCENE_PLE_MAX_ENVS ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: version %u with %u environments, ignored\n", name, version, envs );
		} else {
			words.assign( 8 + envs * SCENE_PLE_ENV_WORDS, 0u );
			words[0] = envs;
			memcpy( &words[1], p + 12, 5 * 4 );	// origin, units per cm, sun lux
			memcpy( &scenePleSunLux, p + 28, 4 );
			p += 32;
			qboolean ok = qtrue;
			for ( uint32_t e = 0; e < envs && ok; e++ ) {
				const size_t t = 8 + e * SCENE_PLE_ENV_WORDS;	// (an index: the arrays' resizes move the vector)
				uint32_t n[5];
				if ( p + ( 3 + 4 + 16 + 5 ) * 4 > end ) {
					ok = qfalse;
					break;
				}
				memcpy( &words[t], p, 12 );				// position
				memcpy( &words[t + 4], p + 12, 16 );		// grid size, hierarchy
				memcpy( &words[t + 8], p + 28, 64 );		// matrix
				memcpy( n, p + 92, 20 );
				p += 112;
				// probes x 4, coefficients x 9, bounds x 12, index, occluder triangles x 9, occluder ranges
				const uint64_t counts[6] = { (uint64_t)n[0] * 4, (uint64_t)n[0] * 9, (uint64_t)n[1] * 12, n[2], (uint64_t)n[3] * 9, n[4] };
				for ( int a = 0; a < 6; a++ ) {
					const uint64_t bytes = counts[a] * 4u;
					if ( (uint64_t)( end - p ) < bytes || words.size() + counts[a] > 0x3fffffffu ) {
						ok = qfalse;
						break;
					}
					words[t + 24 + a] = (uint32_t)words.size();
					words.resize( words.size() + (size_t)counts[a] );
					if ( bytes )
						memcpy( &words[words[t + 24 + a]], p, (size_t)bytes );
					p += bytes;
				}
				words[t + 30] = n[1];
				if ( ok )
					ri.Printf( PRINT_ALL, "scene probe GI %s: environment %u, %u probes, %u bounds, grid %u x %u x %u\n", name, e,
						n[0], n[1], words[t + 4], words[t + 5], words[t + 6] );
			}
			if ( !ok ) {
				ri.Printf( PRINT_WARNING, "WARNING: %s: truncated, ignored\n", name );
				words.clear();
				scenePleSunLux = 0.0f;
			}
		}
	}
	if ( buf )
		ri.FS_FreeFile( buf );
	if ( words.empty() || !Scene_StaticBuffer( &sc.ple, words.data(), (VkDeviceSize)words.size() * 4u,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene probe GI" ) ) {
		scenePleSunLux = 0.0f;
		Scene_StaticBuffer( &sc.ple, zero, sizeof( zero ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene probe GI (none)" );
	}
	Scene_LoadPleReflection( scnName );
}
