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
vk_scene_w3.cpp: the render scene's Witcher 3 side files (tools/levelport/w3scene.py, w3trees.py): maps/<map>.xw3t
terrain tiles, maps/<map>.xw3b layer blends and billboard cards. Moved from vk_scene.cpp.
*/
#include "vk_scene_local.h"

/*
maps/<map>.xw3b (tools/levelport/w3scene.py, Witcher 3 levels): the materials that blend a texture array's
layers by vertex colour (the game's pbr_vert_blend_colorize). A 16-byte header ("XW3B", version 1, the scene's
material count, its vertex count), then per vertex 12 bytes in the scene's vertex order (its second UV set, f32 x 2; its
colour as the game stores it, u8 x 4), then per material 1232 bytes (version 2: 1248, four more floats after the 20:
the material's specular colour F0, linear rgb, 0 for the default 0.04, and a pad; a material without layers but with
one gets detail record -5 with it): 20 floats (layers 0 = none, the tinted layers'
first and last, AO power, AO tint rgb, roughness scale, tint rgb, roughness base, the pattern's first and last layer,
its strength, its UV scale, its UV offset xy, 2 pad), then 18 64-byte image paths (8 layers' colour, 8 their normal
with the roughness in alpha, the pattern, its mask). Read by the GPU-driven draws only: the layers' images go into
the slot images (the array's start), the material's detail record says -2 with its 9-vec4 block (scene.frag); the
stream is draw set binding 27, only for a scene that is not streamed (its vertices keep the file's order).
*/
typedef struct { char ident[4]; uint32_t version, materials, vertices; } xw3bHeader_t;

/*
maps/<map>.xw3t (tools/levelport/w3scene.py): Witcher 3 terrain tiles drawn with the game's terrain shader
(as the Witcher 3 Blender tools emulate it: four control taps, each an overlay layer projected on world XY and a
background layer at the tap's UV scale, blended by the slope against the tap's threshold, under the tile's tint). A
16-byte header ("XW3T", version 1 or 2, tile count, layer count n <= 32), then n layers of 4 floats (blend sharpness,
slope base dampening, slope normal dampening, 0; version 2: 8, then specularity, specularity base, falloff, specularity
scale: the layer's specular level as the Blender tools compute it) and 2 x n 64-byte image paths (colour, normal with roughness in alpha), then
per tile: its material index, lattice size m (u32 x 2), the game's map origin x, y (f32, metres: world = scene / 37.5 +
origin), its tint image (64 B), then m x m control words (u16, row y, column x; m = tile resolution + 1, the positive
neighbours' edges stitched in). The tile's vertices hold its UV (0..1 over the tile, +x, +y) in the stream's second UV
set (maps/<map>.xw3b). Each tile material's detail record says -3 with its block: 0 (lattice base, lattice size, tint
image, layers), 1 (origin x, y, 1 with the specular values, 0), 2..33 the layers' values, 34..41 their colour images,
42..49 their normals, 50..81 their specular values (version 2); the lattice at its base, 4 words a vec4, as floats.
*/
typedef struct { char ident[4]; uint32_t version, tiles, layers; } xw3tHeader_t;

static uint32_t Scene_LoadW3Terrain( const char *scnName, std::vector<float> &params, qboolean haveStream )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	uint32_t done = 0;
	int missing = 0;

	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xw3t" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len < (int)sizeof( xw3tHeader_t ) ) {
		if ( buf )
			ri.FS_FreeFile( buf );
		return 0;
	}
	const xw3tHeader_t *h = (const xw3tHeader_t *)buf;
	const byte *end = (const byte *)buf + len;
	if ( memcmp( h->ident, "XW3T", 4 ) || h->version < 1 || h->version > 2 || h->layers == 0 || h->layers > 32
		|| (size_t)len < sizeof( xw3tHeader_t ) + (size_t)h->layers * ( ( h->version == 2 ? 8u : 4u ) * 4u + 2u * 64u ) ) {	// (its layer table whole)
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 or 2 terrain file, ignored\n", name );
	} else if ( !haveStream || !vk.gpuDriven ) {
		ri.Printf( PRINT_ALL, "%s: the terrain shader needs the GPU-driven draws and the vertex stream; drawn plain\n", name );
	} else {
		const float *layerValues = (const float *)( h + 1 );
		const uint32_t stride = h->version == 2 ? 8u : 4u;	// floats a layer
		const byte *paths = (const byte *)( layerValues + stride * h->layers );
		const byte *tile = paths + 2u * h->layers * 64u;
		auto slotIndex = [&]( const byte *p ) -> float {
			char path[65];
			Com_Memcpy( path, p, 64 );
			path[64] = '\0';
			if ( !path[0] )
				return -1.0f;
			qboolean found;
			image_t *image = Scene_Image( path, &found );
			if ( !found ) {
				missing++;
				return -1.0f;
			}
			for ( size_t k = 0; k < sceneSlotImages.size(); k++ )
				if ( sceneSlotImages[k] == image )
					return (float)k;
			sceneSlotImages.push_back( image );
			return (float)( sceneSlotImages.size() - 1 );
		};
		float layerBlock[80 * 4];
		Com_Memset( layerBlock, 0, sizeof( layerBlock ) );
		for ( uint32_t k = 0; k < 32; k++ ) {
			if ( k < h->layers ) {
				Com_Memcpy( &layerBlock[k * 4], &layerValues[k * stride], 16 );
				if ( stride == 8u )	// (version 2: the specular values, block 50..81)
					Com_Memcpy( &layerBlock[( 48 + k ) * 4], &layerValues[k * stride + 4], 16 );
			}
			layerBlock[128 + k] = k < h->layers ? slotIndex( paths + k * 64u ) : -1.0f;
			layerBlock[160 + k] = k < h->layers ? slotIndex( paths + ( h->layers + k ) * 64u ) : -1.0f;
		}
		for ( uint32_t t = 0; t < h->tiles; t++ ) {
			if ( tile + 80 > end )
				break;
			uint32_t mat, m;
			float origin[2];
			Com_Memcpy( &mat, tile, 4 );
			Com_Memcpy( &m, tile + 4, 4 );
			Com_Memcpy( origin, tile + 8, 8 );
			const byte *tint = tile + 16;
			const uint16_t *words = (const uint16_t *)( tile + 80 );
			const byte *next = tile + 80 + (size_t)m * m * 2u;
			if ( m < 2 || m > 4097 || next > end )
				break;
			tile = next;
			if ( mat >= sc.numMaterials )
				continue;
			const size_t n = (size_t)m * m;
			if ( params.size() / 4u + ( n + 3u ) / 4u + 2u + 80u > SCENE_DETAIL_EXACT ) {	// its block indices would not be exact floats
				ri.Printf( PRINT_WARNING, "WARNING: %s: the material detail would pass %u vec4s; tile %u and the rest drawn plain\n", name,
					SCENE_DETAIL_EXACT, t );
				break;
			}
			// the lattice first, on a vec4 boundary (params is a vec4 array: whole vec4s)
			const uint32_t latticeBase = (uint32_t)( params.size() / 4u );
			params.resize( params.size() + ( ( n + 3u ) & ~(size_t)3u ), 0.0f );
			float *L = &params[(size_t)latticeBase * 4u];
			for ( size_t i = 0; i < n; i++ )
				L[i] = (float)words[i];
			float head[8] = { (float)latticeBase, (float)m, slotIndex( tint ), (float)h->layers, origin[0], origin[1],
				stride == 8u ? 1.0f : 0.0f, 0.0f };
			float *d = &params[mat * 4u];
			d[0] = -3.0f;	// a terrain tile: its block (scene.frag)
			d[1] = (float)( params.size() / 4u );
			d[2] = (float)h->layers;
			d[3] = 0.0f;
			params.insert( params.end(), head, head + 8 );
			params.insert( params.end(), layerBlock, layerBlock + 80 * 4 );
			done++;
		}
	}
	if ( done || missing )
		ri.Printf( PRINT_ALL, "scene terrain %s: %u tiles, %u layers, %u slot images in all (%d images missing)\n", name, done,
			h->layers, (uint32_t)sceneSlotImages.size(), missing );
	ri.FS_FreeFile( buf );
	return done;
}

// an IEEE half's value (the billboard records' matches)
static float Scene_Half( const uint32_t hb )
{
	const uint32_t e = ( hb >> 10 ) & 31u, m = hb & 1023u;
	const float v = e == 0 ? (float)m * ( 1.0f / 16777216.0f ) : e == 31 ? 65504.0f : ldexpf( 1.0f + (float)m / 1024.0f, (int)e - 15 );
	return ( hb & 0x8000u ) ? -v : v;
}

void Scene_LoadW3Blend( const char *scnName, std::vector<float> &params )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	static const uint32_t zero[4] = { 0, 0, 0, 0 };
	const void *stream = zero;
	VkDeviceSize streamBytes = sizeof( zero );
	int missing = 0;
	uint32_t numF0 = 0;	// (the materials with a specular colour alone: scene.vert still reads their stream entries)

	sc.numW3Blend = 0;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xw3b" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len >= (int)sizeof( xw3bHeader_t ) ) {
		const xw3bHeader_t *h = (const xw3bHeader_t *)buf;
		// version 2: 24 floats a record (the 20, then the material's specular colour F0, linear rgb, and a pad); version 3:
		// version 2's, then the billboard section (below)
		const uint32_t nf = h->version >= 2 ? 24u : 20u, recSize = nf * 4u + 18u * 64u;
		// (sizes in 64 bits, so a crafted count cannot wrap them on a 32-bit build; need fits once the file holds it)
		const uint64_t need64 = sizeof( xw3bHeader_t ) + (uint64_t)h->vertices * 12u + (uint64_t)h->materials * recSize;
		const size_t need = (size_t)need64;
		if ( memcmp( h->ident, "XW3B", 4 ) || h->version < 1 || h->version > 4 || h->materials != sc.numMaterials || (uint64_t)len < need64 ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 to 4 layer-blend file for this scene's materials, ignored\n", name );
		} else if ( !sceneFileVertices || h->vertices != sceneFileVertices ) {	// (0: the scene streams)
			ri.Printf( PRINT_WARNING, "WARNING: %s: its %u vertices are not this scene's %u (or the scene streams), ignored\n",
				name, h->vertices, sceneFileVertices );
		} else if ( !vk.gpuDriven ) {
			ri.Printf( PRINT_ALL, "%s: layer blends need the GPU-driven draws; drawn with their first layer\n", name );
		} else {
			const byte *mats = (const byte *)buf + sizeof( xw3bHeader_t ) + (size_t)h->vertices * 12u;
			auto slotIndex = [&]( const byte *p ) -> float {
				char path[65];
				Com_Memcpy( path, p, 64 );
				path[64] = '\0';
				if ( !path[0] )
					return -1.0f;
				qboolean found;
				image_t *image = Scene_Image( path, &found );
				if ( !found ) {
					missing++;
					return -1.0f;
				}
				for ( size_t k = 0; k < sceneSlotImages.size(); k++ )
					if ( sceneSlotImages[k] == image )
						return (float)k;
				sceneSlotImages.push_back( image );
				return (float)( sceneSlotImages.size() - 1 );
			};
			for ( uint32_t i = 0; i < h->materials; i++ ) {
				const byte *rec = mats + (size_t)i * recSize;
				const byte *recPaths = rec + nf * 4u;
				float p[24];
				Com_Memset( p, 0, sizeof( p ) );
				Com_Memcpy( p, rec, nf * 4u );
				const int layers = (int)p[0];
				const qboolean hasF0 = ( p[20] > 0.0f || p[21] > 0.0f || p[22] > 0.0f ) ? qtrue : qfalse;
				if ( p[23] == 3.0f && params[i * 4u] == 0.0f ) {
					// flowing water (Witcher 3 waterfall strips and dam sheets; -8, scene.frag): 0 (scroll speed u, v
					// in tiles a second, tiling u, v); the sheet's opacity scale in md.z
					float block[4] = { p[1], p[2], p[3] > 0.0f ? p[3] : 1.0f, p[4] > 0.0f ? p[4] : 1.0f };
					float *d = &params[i * 4u];
					d[0] = -8.0f;
					d[1] = (float)( params.size() / 4u );
					d[2] = p[5] > 0.0f ? p[5] : 1.0f;
					d[3] = 0.0f;
					params.insert( params.end(), block, block + 4 );
					sc.numW3Blend++;
					continue;
				}
				if ( p[23] == 2.0f && params[i * 4u] == 0.0f ) {
					// a slope layer (snow.w2mg; -7, scene.frag): 0 (the up-facing range's low and high cosine, its UV
					// tiling u, v), 1 (its colour image, its normal image: -1 none), 2 F0
					float block[12] = { p[1], p[2], p[3], p[4], slotIndex( recPaths ), slotIndex( recPaths + 64 ), 0.0f, 0.0f,
						p[20], p[21], p[22], 0.0f };
					if ( block[4] < 0.0f )
						continue;
					float *d = &params[i * 4u];
					d[0] = -7.0f;
					d[1] = (float)( params.size() / 4u );
					d[2] = d[3] = 0.0f;
					params.insert( params.end(), block, block + 12 );
					sc.numW3Blend++;
					continue;
				}
				if ( p[23] == 1.0f && params[i * 4u] == 0.0f ) {
					// a wear-paint material (pbr_std_wear_paint; -6, scene.frag): 0 (paint opacity, its low and high
					// vertex blue, its sharpness), 1 (paint mask inverted, paint tiling, the masks' tiling, vertex AO),
					// 2 (paint tint, wear opacity), 3 (its low and high vertex wear, sharpness, wear mask inverted), 4 (wear
					// tint, brightness), 5 (wear saturation, the untinted base, the masks, the pattern: -1 none), 6 F0
					const uint32_t wt = (uint32_t)p[19];
					float block[28] = {
						p[1], p[2], p[3], p[4],
						p[5], p[9], p[18], p[17],
						p[6], p[7], p[8], p[10],
						p[11], p[12], p[13], p[14],
						( wt & 255u ) / 255.0f, ( ( wt >> 8 ) & 255u ) / 255.0f, ( ( wt >> 16 ) & 255u ) / 255.0f, p[15],
						p[16], slotIndex( recPaths ), slotIndex( recPaths + 64 ), slotIndex( recPaths + 2 * 64 ),
						p[20], p[21], p[22], 0.0f };
					float *d = &params[i * 4u];
					d[0] = -6.0f;
					d[1] = (float)( params.size() / 4u );
					d[2] = d[3] = 0.0f;
					params.insert( params.end(), block, block + 28 );
					sc.numW3Blend++;
					continue;
				}
				if ( ( layers <= 0 || layers > 8 ) && hasF0 && params[i * 4u] == 0.0f ) {
					// a Witcher 3 material without layers: its specular colour alone (-5, scene.frag)
					float *d = &params[i * 4u];
					d[0] = -5.0f;
					d[1] = p[20];
					d[2] = p[21];
					d[3] = p[22];
					numF0++;
					continue;
				}
				if ( layers <= 0 || layers > 8 )
					continue;
				// the block: 0 (layers, tinted first, last, AO power), 1 (AO tint, roughness scale), 2 (tint, roughness base),
				// 3 (pattern first, last, strength, UV scale), 4 (pattern UV offset, its image, its mask), 5-6 the layers'
				// colour images, 7-8 their normals (-1: none), 9 its specular colour (version 2; 0: the default 0.04)
				float block[40];
				Com_Memcpy( block, p, 18 * sizeof( float ) );
				block[18] = slotIndex( recPaths + 16 * 64 );
				block[19] = slotIndex( recPaths + 17 * 64 );
				int found = 0;
				for ( int j = 0; j < 8; j++ ) {
					block[20 + j] = j < layers ? slotIndex( recPaths + j * 64 ) : -1.0f;
					block[28 + j] = j < layers ? slotIndex( recPaths + ( 8 + j ) * 64 ) : -1.0f;
					found += block[20 + j] >= 0.0f ? 1 : 0;
				}
				block[36] = p[20];
				block[37] = p[21];
				block[38] = p[22];
				block[39] = 0.0f;
				if ( !found )
					continue;
				float *d = &params[i * 4u];
				d[0] = -2.0f;	// a layer blend: its block (scene.frag)
				d[1] = (float)( params.size() / 4u );
				d[2] = (float)layers;
				d[3] = 0.0f;
				params.insert( params.end(), block, block + 40 );
				sc.numW3Blend++;
			}
			if ( numF0 )
				ri.Printf( PRINT_ALL, "%s: %u materials with their own specular colour\n", name, numF0 );
			// version 3: Witcher 3 SpeedTree billboards (tools/levelport w3scene.py writes them): u32 count,
			// then 416-byte records: type (1 the card turned to the viewer, 2 the top card), material, views, a0 (radians),
			// the pitch band's start and end (degrees), 2 pad, 16 views x 6 floats (card (s, t) to the atlas: u = m0 s +
			// m1 t + m2, v = m3 s + m4 t + m5). Detail -9 / -10 (scene.vert, scene.frag), block: 0 (a0, the band in
			// radians, views), then two vec4s a view (m0..m3; m4, m5)
			if ( h->version >= 3 && (uint64_t)len >= need64 + 4u ) {
				uint32_t count, numCards = 0, numTops = 0, numFixed = 0;
				Com_Memcpy( &count, (const byte *)buf + need, 4 );
				const byte *recs = (const byte *)buf + need + 4u;
				if ( (uint64_t)len < need64 + 4u + (uint64_t)count * 416u ) {
					ri.Printf( PRINT_WARNING, "WARNING: %s: its billboard section is cut short, ignored\n", name );
					count = 0;
				}
				for ( uint32_t r = 0; r < count; r++ ) {
					const byte *rec = recs + (size_t)r * 416u;
					uint32_t type, mat, views;
					float a0, fade[2];
					uint32_t fix[2];	// (the pads: a match to the 3D tree, below)
					Com_Memcpy( &type, rec, 4 );
					Com_Memcpy( &mat, rec + 4, 4 );
					Com_Memcpy( &views, rec + 8, 4 );
					Com_Memcpy( &a0, rec + 12, 4 );
					Com_Memcpy( fade, rec + 16, 8 );
					Com_Memcpy( fix, rec + 24, 8 );
					if ( mat >= h->materials || ( type != 1 && type != 2 ) || ( type == 1 && ( views < 1 || views > 16 ) ) )
						continue;
					float block[4 + 16 * 8 + 4];
					Com_Memset( block, 0, sizeof( block ) );
					block[0] = a0;
					block[1] = fade[0] * (float)( M_PI / 180.0 );
					block[2] = fade[1] * (float)( M_PI / 180.0 );
					block[3] = type == 1 ? (float)views : 0.0f;
					uint32_t nvec = 1;
					if ( type == 1 ) {
						for ( uint32_t v = 0; v < views; v++ )
							Com_Memcpy( &block[4 + v * 8], rec + 32 + v * 24u, 24 );
						nvec = 1 + 2 * views;
					}
					// the card's match to its 3D tree (w3cardmatch.py): the pads hold four halves, the
					// multipliers of its ambient light (r, g, b) and of its sun light (a card, lit as one sheet, missed the
					// canopy's shadow on its own inner leaves: pale against its 3D tree, popping at the switch); zero:
					// none. The block's last vec4 (detail z: its index in the block, 0 none)
					uint32_t fixAt = 0;
					if ( fix[0] | fix[1] ) {
						const float f[4] = { Scene_Half( fix[0] & 0xffffu ), Scene_Half( fix[0] >> 16 ),
							Scene_Half( fix[1] & 0xffffu ), Scene_Half( fix[1] >> 16 ) };
						Com_Memcpy( &block[nvec * 4u], f, sizeof( f ) );
						fixAt = nvec++;
						numFixed++;
					}
					float *d = &params[mat * 4u];
					d[0] = type == 1 ? -9.0f : -10.0f;
					d[1] = (float)( params.size() / 4u );
					d[2] = (float)fixAt;
					d[3] = 0.0f;
					params.insert( params.end(), block, block + nvec * 4u );
					if ( type == 1 )
						numCards++;
					else
						numTops++;
				}
				ri.Printf( PRINT_ALL, "%s: %u billboard cards, %u top cards (%u matched to their 3D trees)\n", name, numCards, numTops, numFixed );
			}
			stream = (const byte *)buf + sizeof( xw3bHeader_t );	// (kept when the layer blends or the terrain use it)
			streamBytes = (VkDeviceSize)h->vertices * 12u;
		}
	}
	if ( sc.numW3Blend || missing )
		ri.Printf( PRINT_ALL, "scene layer blends %s: %u materials, %u slot images in all (%d images missing)\n", name, sc.numW3Blend,
			(uint32_t)sceneSlotImages.size(), missing );
	// the terrain (maps/<map>.xw3t) reads the same stream: its tile UV in the second UV set
	const uint32_t terrain = Scene_LoadW3Terrain( scnName, params, streamBytes > 16 ? qtrue : qfalse );
	if ( !sc.numW3Blend && !numF0 && !terrain && !Scene_WindTrees() ) {	// (the trees' wind reads it too)
		stream = zero;
		streamBytes = sizeof( zero );
	}
	if ( !Scene_StaticBuffer( &sc.w3Stream, stream, MAX( streamBytes, (VkDeviceSize)16 ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene layer-blend stream" ) )
		sc.numW3Blend = 0;
	if ( buf )
		ri.FS_FreeFile( buf );
}

/*
SpeedTree 7 wind (maps/<map>.xw3b version 4, r_sceneTreeWind): each tree's CWind parameters as the game ships them
(w3trees.py wind_record: SCENE_WIND_RECORD floats). Each frame Scene_WindFrame evaluates them at the wind's strength
into SCENE_WIND_VEC4 vec4s a tree (the constants SpeedTree's vertex shader reads; Unity's SpeedTreeWind.cginc names
them), after a header block: scene.vert's tree t + 1 reads block t + 1. The oscillators' clocks run on each one's
frequency at the strength now (integrated over the game's clock, so a change of strength does not jump their phase).
Record layout (w3trees.py): 0 options (bits), 1 frequencies [10 oscillators][10 points], 101 global height, its exponent,
103 global distance [10], 113 global direction adherence [10], 123 and 156 the branch levels (turbulence, twitch,
twitch frequency scale, distance [10], direction adherence [10], whip [10]), 189 and 240 the leaf groups (twitch
sharpness, ripple distance [10], tumble flip [10], twist [10], direction adherence [10], twitch throw [10]), 291 frond
ripple distance [10], tile, lighting scalar, 303 gust frequency, strength min, max, duration min, max, rise, fall, 310
strength response, direction response, anchor offset, anchor distance scale, 314 branch anchor xyz, the longest
level-1 branch, 318 the leaf anchor offsets' scale (the stream's 7-bit offsets).
Oscillators (the frequencies' rows): 0 global, 1 branch 1, 2 branch 2, 3-5 leaf 1 ripple, tumble, twitch, 6-8 leaf 2's,
9 frond ripple.
*/
static std::vector<float>	sceneWindRecs;		// SCENE_WIND_RECORD floats a tree
static std::vector<double>	sceneWindTimes;		// a tree's 10 oscillator clocks and its turbulence's global clock
static int					sceneWindLast = -1;	// the game time (ms) they were advanced to (-1: start from it)

uint32_t Scene_WindTrees( void )
{
	return (uint32_t)( sceneWindRecs.size() / SCENE_WIND_RECORD );
}

void Scene_WindFree( void )
{
	sceneWindRecs.clear();
	sceneWindTimes.clear();
	sceneWindLast = -1;
}

// a 10-point curve over the strength 0..1
static float Scene_WindCurve( const float *c, float s )
{
	const float x = Com_Clamp( 0.0f, 1.0f, s ) * 9.0f;
	const int i = MIN( (int)x, 8 );
	return c[i] + ( c[i + 1] - c[i] ) * ( x - (float)i );
}

// an integer hashed to 0..1 (the gusts' strengths)
static float Scene_WindHash( uint32_t x )
{
	x ^= x >> 16;
	x *= 0x7feb352du;
	x ^= x >> 15;
	x *= 0x846ca68bu;
	x ^= x >> 16;
	return (float)( x & 0xffffffu ) / 16777215.0f;
}

/*
Scene_WindFrame: out = the frame's wind buffer (SCENE_MAX_WIND_TREES + 1 blocks). strength: the wind's (the game's
weather windScale times its environment's windStrengthOverride; 0: still: the header's x 0 and scene.vert leaves every
vertex as it is); gust: the share of each tree's own gusts. Gusts: the game's (SpeedTree's CWind) come at random; here
each tree's come at its m_fGustFrequency on one deterministic sequence of the game's clock, each reaching a strength
from its [min, max] (times gust), rising and falling in the ratio of its rise and fall scalars, and scaling the
strength by (1 + it). The clocks are kept modulo 1000 s: every multiplier SpeedTree's shader puts on them (0.689, 0.75,
0.87, 0.1, 0.01, the twitch scales to 0.001) times 1000 is a whole number, so the wrap is seamless, and float32 keeps
0.1 ms there.
*/
void Scene_WindFrame( float *out, int timeMs, float strength, float gust, const float dirW[3] )
{
	const uint32_t nt = Scene_WindTrees();
	Com_Memset( out, 0, SCENE_WIND_VEC4 * 16 );
	if ( !nt || strength <= 0.0f )
		return;
	const double now = (double)(uint32_t)timeMs * 0.001;
	const double dt = sceneWindLast < 0 ? -1.0 : (double)( timeMs - sceneWindLast ) * 0.001;
	const qboolean restart = ( dt < 0.0 || dt > 1.0 ) ? qtrue : qfalse;	// (the first frame, a replay's jump back, a stall)
	sceneWindLast = timeMs;
	out[0] = 1.0f;
	out[1] = (float)nt;
	for ( uint32_t t = 0; t < nt; t++ ) {
		const float *R = &sceneWindRecs[(size_t)t * SCENE_WIND_RECORD];
		double *T = &sceneWindTimes[(size_t)t * 11u];
		// the strength now, with this tree's gusts
		float s = Com_Clamp( 0.0f, 1.0f, strength );
		if ( gust > 0.0f && R[303] > 0.0f ) {
			const double period = 1.0 / MAX( R[303], 0.02f ), x = now / period;
			const uint32_t k = (uint32_t)(int64_t)floor( x );
			const float u = (float)( x - floor( x ) );
			const float a = ( R[304] + ( R[305] - R[304] ) * Scene_WindHash( k * 2654435761u ^ 0x9e3779b9u ) ) * gust;
			const float rise = MAX( R[308], 0.05f ), fall = MAX( R[309], 0.05f );
			const float top = rise / ( rise + fall );	// (the gust's peak within its period)
			const float e = u < top ? u / top : 1.0f - ( u - top ) / ( 1.0f - top );
			s = Com_Clamp( 0.0f, 1.0f, s * ( 1.0f + a * e * e * ( 3.0f - 2.0f * e ) ) );
		}
		for ( int o = 0; o < 10; o++ ) {
			const double fq = Scene_WindCurve( R + 1 + o * 10, s );
			T[o] = restart ? fmod( fq * now, 1000.0 ) : fmod( T[o] + fq * dt, 1000.0 );
		}
		T[10] = fmod( now, 1000.0 );
		float *B = out + (size_t)( t + 1 ) * SCENE_WIND_VEC4 * 4u;
		const float *b1 = R + 123, *b2 = R + 156, *l1 = R + 189, *l2 = R + 240;
		// 0: the wind's direction (world), the strength (_ST_WindVector)
		B[0] = dirW[0]; B[1] = dirW[1]; B[2] = dirW[2]; B[3] = s;
		// 1: _ST_WindGlobal (its clock, distance, 1 / height, the height's exponent)
		B[4] = (float)T[0]; B[5] = Scene_WindCurve( R + 103, s ); B[6] = 1.0f / MAX( R[101], 0.01f ); B[7] = R[102];
		// 2: _ST_WindBranch for both levels (clock, distance)
		B[8] = (float)T[1]; B[9] = Scene_WindCurve( b1 + 3, s ); B[10] = (float)T[2]; B[11] = Scene_WindCurve( b2 + 3, s );
		// 3: _ST_WindBranchTwitch (twitch, its frequency scale) for both
		B[12] = b1[1]; B[13] = b1[2]; B[14] = b2[1]; B[15] = b2[2];
		// 4: _ST_WindBranchWhip for both; _ST_WindTurbulences
		B[16] = Scene_WindCurve( b1 + 23, s ); B[17] = Scene_WindCurve( b2 + 23, s ); B[18] = b1[0]; B[19] = b2[0];
		// 5: _ST_WindBranchAdherences (global, branch 1, branch 2); _ST_WindAnimation.x (the turbulence's global clock)
		B[20] = Scene_WindCurve( R + 113, s ); B[21] = Scene_WindCurve( b1 + 13, s ); B[22] = Scene_WindCurve( b2 + 13, s );
		B[23] = (float)T[10];
		// 6: _ST_WindBranchAnchor (the tree's, mesh-local, normalized; the longest level-1 branch)
		{
			const float l = sqrtf( R[314] * R[314] + R[315] * R[315] + R[316] * R[316] );
			B[24] = l > 0.0f ? R[314] / l : 0.0f; B[25] = l > 0.0f ? R[315] / l : 0.0f; B[26] = l > 0.0f ? R[316] / l : 1.0f;
			B[27] = R[317];
		}
		// 7-9 leaf group 1, 10-12 leaf group 2: ripple (clock, distance), tumble (clock, flip, twist, adherence), twitch
		// (throw, sharpness, clock)
		for ( int g = 0; g < 2; g++ ) {
			const float *L = g ? l2 : l1;
			float *D = B + 28 + g * 12;
			const int o = 3 + g * 3;
			D[0] = (float)T[o]; D[1] = Scene_WindCurve( L + 1, s ); D[2] = D[3] = 0.0f;
			D[4] = (float)T[o + 1]; D[5] = Scene_WindCurve( L + 11, s ); D[6] = Scene_WindCurve( L + 21, s ); D[7] = Scene_WindCurve( L + 31, s );
			D[8] = Scene_WindCurve( L + 41, s ); D[9] = L[0]; D[10] = (float)T[o + 2]; D[11] = 0.0f;
		}
		// 13: _ST_WindFrondRipple (clock, distance, tile, lighting scalar)
		B[52] = (float)T[9]; B[53] = Scene_WindCurve( R + 291, s ); B[54] = R[301]; B[55] = R[302];
		// 14: the options' bits (as stored), the leaf anchor offsets' scale
		B[56] = R[0]; B[57] = R[318]; B[58] = B[59] = 0.0f;
	}
}

/*
Scene_MarkCardMeshes: the meshes whose last LOD is a Witcher 3 billboard (maps/<map>.xw3b version 3: every part of it a
card's or a top card's material, one a card's) get mesh word 6 (scene_cull.comp's M.lods.z) set. A card turns to face
the eye in every pass, so as a sun caster it shadowed its own tree's 3D leaves in a shape that moved with the eye and
changed at once where the casters' coarser LOD (r_sceneShadowLod) switched to it: the 3D trees seen from the air
brightened and darkened a frame to the next (frame-locked fly-throughs: the worst frames' unexplained share
0.38% against 0.06% with 3D trees). The cull keeps such a mesh's caster at its coarsest 3D LOD while the eye's own LOD
is 3D. Before the meshes are uploaded (the layer-blend loader reads the same records later, for the materials).
*/
void Scene_MarkCardMeshes( const char *scnName, xscnMesh_t *meshes, uint32_t numMeshes, const xscnLod_t *lods,
	const xscnPart_t *parts, uint32_t numMats )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xw3b" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len < (int)sizeof( xw3bHeader_t ) ) {
		if ( buf )
			ri.FS_FreeFile( buf );
		return;
	}
	const xw3bHeader_t *h = (const xw3bHeader_t *)buf;
	const uint64_t need64 = sizeof( xw3bHeader_t ) + (uint64_t)h->vertices * 12u + (uint64_t)h->materials * ( 24u * 4u + 18u * 64u );
	const size_t need = (size_t)need64;	// (used once the file is known to hold it)
	uint32_t count = 0, marked = 0;
	const qboolean ok = ( !memcmp( h->ident, "XW3B", 4 ) && ( h->version == 3 || h->version == 4 ) && h->materials == numMats
		&& (uint64_t)len >= need64 + 4u ) ? qtrue : qfalse;
	if ( ok ) {
		Com_Memcpy( &count, (const byte *)buf + need, 4 );
		if ( (uint64_t)len < need64 + 4u + (uint64_t)count * 416u )
			count = 0;
	}
	// version 4: the SpeedTree wind after the cards (tools/levelport w3scene.py, w3trees.py's wind section): u32 trees, u32
	// meshes, per tree SCENE_WIND_RECORD floats, per mesh u32 mesh, u32 tree. The mesh gets word 7 (lods.w) = tree + 1;
	// its vertices' wind words are in the stream (scene.vert). Read here, before the meshes are uploaded
	if ( ok && h->version == 4 ) {
		uint32_t cards = 0;
		Com_Memcpy( &cards, (const byte *)buf + need, 4 );
		const uint64_t at = need64 + 4u + (uint64_t)cards * 416u;
		uint32_t nt = 0, nm = 0;
		if ( (uint64_t)len >= at + 8u ) {
			Com_Memcpy( &nt, (const byte *)buf + (size_t)at, 4 );
			Com_Memcpy( &nm, (const byte *)buf + (size_t)at + 4, 4 );
		}
		if ( nt > SCENE_MAX_WIND_TREES || (uint64_t)len < at + 8u + (uint64_t)nt * SCENE_WIND_RECORD * 4u + (uint64_t)nm * 8u ) {
			ri.Printf( PRINT_WARNING, "WARNING: %s: its wind section (%u trees, %u meshes) is cut short or too large, ignored\n", name, nt, nm );
		} else if ( nt ) {
			const byte *w = (const byte *)buf + (size_t)at + 8u;
			sceneWindRecs.resize( (size_t)nt * SCENE_WIND_RECORD );
			Com_Memcpy( sceneWindRecs.data(), w, (size_t)nt * SCENE_WIND_RECORD * 4u );
			sceneWindTimes.assign( (size_t)nt * 11u, 0.0 );
			sceneWindLast = -1;
			const byte *m = w + (size_t)nt * SCENE_WIND_RECORD * 4u;
			uint32_t windMeshes = 0;
			for ( uint32_t k = 0; k < nm; k++ ) {
				uint32_t pair[2];
				Com_Memcpy( pair, m + (size_t)k * 8u, 8 );
				if ( pair[0] < numMeshes && pair[1] < nt ) {
					meshes[pair[0]].pad[1] = pair[1] + 1u;
					windMeshes++;
				}
			}
			ri.Printf( PRINT_ALL, "%s: SpeedTree wind for %u trees, %u meshes (r_sceneTreeWind)\n", name, nt, windMeshes );
		}
	}
	if ( count ) {
		std::vector<byte> kind( numMats, 0 );	// 1 a card's material, 2 a top card's
		for ( uint32_t r = 0; r < count; r++ ) {
			const byte *rec = (const byte *)buf + need + 4u + (size_t)r * 416u;
			uint32_t type, mat;
			Com_Memcpy( &type, rec, 4 );
			Com_Memcpy( &mat, rec + 4, 4 );
			if ( mat < numMats && ( type == 1 || type == 2 ) )
				kind[mat] = (byte)type;
		}
		for ( uint32_t i = 0; i < numMeshes; i++ ) {
			if ( meshes[i].numLods < 2 )
				continue;
			const xscnLod_t &L = lods[meshes[i].firstLod + meshes[i].numLods - 1];
			bool card = false, all = L.numParts != 0;
			for ( uint32_t p = L.firstPart; all && p < L.firstPart + L.numParts; p++ ) {
				const uint32_t m = parts[p].material;
				all = m < numMats && kind[m] != 0;
				card = card || ( all && kind[m] == 1 );
			}
			if ( all && card ) {
				meshes[i].pad[0] = 1;
				marked++;
			}
		}
		ri.Printf( PRINT_ALL, "%s: %u meshes end in a billboard card (their sun casters stay 3D while the eye's LOD is)\n", name, marked );
	}
	ri.FS_FreeFile( buf );
}
