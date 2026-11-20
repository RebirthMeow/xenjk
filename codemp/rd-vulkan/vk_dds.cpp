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

// DDS textures and layer arrays (moved out of vk_image.cpp; vk_image.cpp calls R_LoadDDSImage before its other loaders)
#include "tr_local.h"

/*
xenjk: DDS textures. A texture name (any extension) is first looked up as "<name>.dds"; a BC1, BC3, BC4, BC5 or BC7
file (legacy FourCC or DX10 header, sRGB variants read as UNORM like every other texture here) is uploaded as stored:
no decode, resample, light scale or runtime mips (the pack builder makes the mips with R_MipMap's r_simpleMipMaps box
filter), and not held to MAX_TEXTURE_SIZE: up to the device's limit, or r_ddsMaxSize, by dropping top mips. Picmip
drops mips the same way. Anything else (no file, other formats, broken files) falls back to the normal loaders.
r_ddsTextures 0 ignores DDS files.
*/
#define MAKEFOURCC_DDS( a, b, c, d ) ( (uint32_t)(a) | ( (uint32_t)(b) << 8 ) | ( (uint32_t)(c) << 16 ) | ( (uint32_t)(d) << 24 ) )

static VkFormat R_DDSFormat( const byte *b, int len, int *dataOffset ) {
	const uint32_t fourCC = b[84] | ( b[85] << 8 ) | ( b[86] << 16 ) | ( (uint32_t)b[87] << 24 );
	*dataOffset = 128;
	// a cubemap or volume file goes to the other loaders (it would be read as a 2D texture from the wrong bytes)
	const uint32_t caps2 = b[112] | ( b[113] << 8 ) | ( b[114] << 16 ) | ( (uint32_t)b[115] << 24 );
	if ( caps2 & ( 0x200u | 0x200000u ) )	// DDSCAPS2_CUBEMAP, DDSCAPS2_VOLUME
		return VK_FORMAT_UNDEFINED;
	if ( fourCC == MAKEFOURCC_DDS( 'D', 'X', '1', '0' ) ) {
		if ( len < 148 )
			return VK_FORMAT_UNDEFINED;
		// a 2D texture only: resource dimension TEXTURE2D (3), not a cube (misc flag 4), array size 1
		if ( ( b[132] | ( b[133] << 8 ) ) != 3 || ( b[136] & 4 ) || ( b[140] | ( b[141] << 8 ) | ( b[142] << 16 ) | ( b[143] << 24 ) ) != 1 )
			return VK_FORMAT_UNDEFINED;
		*dataOffset = 148;
		switch ( b[128] | ( b[129] << 8 ) ) {   // DXGI_FORMAT
			case 71: case 72: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			case 77: case 78: return VK_FORMAT_BC3_UNORM_BLOCK;
			case 80: return VK_FORMAT_BC4_UNORM_BLOCK;
			case 83: return VK_FORMAT_BC5_UNORM_BLOCK;
			case 98: case 99: return VK_FORMAT_BC7_UNORM_BLOCK;
			default: return VK_FORMAT_UNDEFINED;
		}
	}
	if ( fourCC == MAKEFOURCC_DDS( 'D', 'X', 'T', '1' ) ) return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	if ( fourCC == MAKEFOURCC_DDS( 'D', 'X', 'T', '5' ) ) return VK_FORMAT_BC3_UNORM_BLOCK;
	if ( fourCC == MAKEFOURCC_DDS( 'A', 'T', 'I', '1' ) || fourCC == MAKEFOURCC_DDS( 'B', 'C', '4', 'U' ) ) return VK_FORMAT_BC4_UNORM_BLOCK;
	if ( fourCC == MAKEFOURCC_DDS( 'A', 'T', 'I', '2' ) || fourCC == MAKEFOURCC_DDS( 'B', 'C', '5', 'U' ) ) return VK_FORMAT_BC5_UNORM_BLOCK;
	return VK_FORMAT_UNDEFINED;
}

// imageName: the image_t's name (the ORM path names it <map>_ORMS); fileName: the texture the shader names. A character
// texture in a stored pk3 entry streams (vk_stream.cpp): only its header and mip tail are read here.
// a DDS's range-read buffer (a streamed texture's tail, or a small texture whole): on Windows its own pages, released
// after the upload, outside the zone's heap (a crash: a tail's temp-hunk block was found decommitted inside
// vk_upload_image_data's staging waits, by nothing the renderer frees); where VirtualAlloc fails, and elsewhere, the
// temp hunk as before. *pages says which, for DDS_FreeRange
static byte *DDS_AllocRange( int bytes, qboolean *pages ) {
#ifdef _WIN32
	byte *p = (byte *)VirtualAlloc( NULL, (SIZE_T)bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
	if ( p ) {
		*pages = qtrue;
		return p;
	}
#endif
	*pages = qfalse;
	return (byte *)ri.Hunk_AllocateTempMemory( bytes );
}

static void DDS_FreeRange( byte *p, qboolean pages ) {
#ifdef _WIN32
	if ( pages ) {
		VirtualFree( p, 0, MEM_RELEASE );
		return;
	}
#endif
	ri.Hunk_FreeTempMemory( p );
}

// vk_vram_fit: a block-compressed chain's bytes from a level down
struct ddsFit_t {
	int				blockBytes, width, height, mips;
};
static VkDeviceSize R_DDSFitBytes( int skip, const void *ctx ) {
	const ddsFit_t *f = (const ddsFit_t *)ctx;
	VkDeviceSize bytes = 0;
	for ( int i = skip; i < f->mips; i++ )
		bytes += (VkDeviceSize)vk_block_level_size( f->blockBytes, MAX( f->width >> i, 1 ), MAX( f->height >> i, 1 ) );
	return bytes;
}

#define DDS_MAX_SIDE 32768	// past it a level's byte count overflows an int

static image_t *R_LoadDDSImageFrom( const char *imageName, const char *fileName, imgFlags_t flags, uint32_t type, qboolean allowStream ) {
	qboolean dataPages = qfalse;
	uint32_t deviceMax;
	char ddsName[MAX_QPATH + 8];	// a 61-63 character name plus ".dds" (R_LoadImage's va() names are not held to MAX_QPATH either)
	byte header[148];
	byte *b = NULL, *data = NULL;
	int len, dataOffset, w, h, mips, skip, n, levelBytes, i, blockBytes, limit, file, tailLevel;
	uint32_t levelOffset[16];
	VkFormat format;
	VkFormatProperties props;
	image_t *image;

	if ( !r_ddsTextures || !r_ddsTextures->integer || !fileName || !fileName[0] || fileName[0] == '*' )
		return NULL;
	COM_StripExtension( fileName, ddsName, sizeof( ddsName ) );
	Q_strcat( ddsName, sizeof( ddsName ), ".dds" );
	// a streamed texture reads its header by range; anything else the whole file
	file = allowStream ? R_StreamOpen( ddsName, fileName, flags, &len ) : 0;
	// a stored DDS without mipmaps (FMA's nomipmaps maps) reads its header and then only the level it uploads
	// by range; the whole-file read took 0.4-0.6 s for an 8192 map of which r_ddsMaxSize keeps the 4096 level
	qboolean rangeOnly = qfalse;
	if ( !file && allowStream && vk.active && !( flags & ( IMGFLAG_MIPMAP | IMGFLAG_CUBEMAP | IMGFLAG_ARRAY | IMGFLAG_STORAGE ) )
		&& r_ddsRangeRead->integer && ri.FS_RangeOpen && ri.FS_RangeRead && ri.FS_RangeClose ) {
		file = ri.FS_RangeOpen( ddsName, &len );
		rangeOnly = file ? qtrue : qfalse;
	}
	if ( file && ( len < 128 || ri.FS_RangeRead( file, 0, MIN( len, (int)sizeof( header ) ), header ) < 0 ) ) {
		ri.FS_RangeClose( file );
		file = 0;
	}
	// r_loadProfile above 1: an image whose load takes over 5 ms prints LOADIMG with its read and the rest
	const double tProf0 = ( r_loadProfile->integer > 1 ) ? R_SunProfNow() : 0.0;
	double tProfRead = 0.0;
	if ( file ) {
		b = header;
	} else {
		len = ri.FS_ReadFile( ddsName, (void **)&b );
		if ( len <= 0 || !b )
			return NULL;
		if ( tProf0 > 0.0 )
			tProfRead = R_SunProfNow() - tProf0;
	}
#define DDS_FAIL() do { if ( file ) ri.FS_RangeClose( file ); else ri.FS_FreeFile( b ); return NULL; } while ( 0 )

	format = VK_FORMAT_UNDEFINED;
	if ( len >= 128 && !memcmp( b, "DDS ", 4 ) )
		format = R_DDSFormat( b, len, &dataOffset );
	if ( format == VK_FORMAT_UNDEFINED ) {
		ri.Printf( PRINT_DEVELOPER, "DDS: %s: not a BC1/3/4/5/7 DDS, using the other loaders\n", ddsName );
		DDS_FAIL();
	}
	qvkGetPhysicalDeviceFormatProperties( vk.physical_device, format, &props );
	if ( !( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) ) {
		ri.Printf( PRINT_DEVELOPER, "DDS: %s: format %i not sampled by this device\n", ddsName, (int)format );
		DDS_FAIL();
	}
	{	// this device's (not cached: r_device can change at a vid_restart)
		VkPhysicalDeviceProperties dp;
		qvkGetPhysicalDeviceProperties( vk.physical_device, &dp );
		deviceMax = dp.limits.maxImageDimension2D;
	}

	h = b[12] | ( b[13] << 8 ) | ( b[14] << 16 ) | ( b[15] << 24 );
	w = b[16] | ( b[17] << 8 ) | ( b[18] << 16 ) | ( b[19] << 24 );
	mips = b[28] | ( b[29] << 8 ) | ( b[30] << 16 ) | ( b[31] << 24 );
	if ( mips < 1 )
		mips = 1;
	if ( w < 1 || h < 1 || ( w & ( w - 1 ) ) || ( h & ( h - 1 ) ) ) {
		ri.Printf( PRINT_WARNING, "DDS: %s: %ix%i is not a power of two, using the other loaders\n", ddsName, w, h );
		DDS_FAIL();
	}
	if ( w > DDS_MAX_SIDE || h > DDS_MAX_SIDE ) {	// the level sizes below are ints
		ri.Printf( PRINT_WARNING, "DDS: %s: %ix%i is larger than %i, using the other loaders\n", ddsName, w, h, DDS_MAX_SIDE );
		DDS_FAIL();
	}
	{	// no more levels than the extent allows (vk_create_image would ask for an invalid count)
		int chain = 1;
		for ( int d = MAX( w, h ); d > 1; d >>= 1 ) chain++;
		if ( mips > chain )
			mips = chain;
	}

	// levels dropped from the top: picmip (as vk_generate_image_upload_data decides it), then the size limit
	skip = 0;
	if ( ( flags & IMGFLAG_PICMIP ) && !( r_smartpicmip && r_smartpicmip->integer && Q_stricmpn( fileName, "textures/", 9 ) ) )
		skip = r_picmip->integer > 0 ? r_picmip->integer : 0;
	limit = (int)deviceMax;
	if ( r_ddsMaxSize->integer > 0 && r_ddsMaxSize->integer < limit )
		limit = r_ddsMaxSize->integer;
	while ( ( MAX( w, h ) >> skip ) > limit )
		skip++;
	// xenjk (vk_vram.cpp): read whole (not streamed), it is admitted against what the VRAM budget leaves; past it the
	// top levels are dropped (to 256 texels across at most), not the session
	if ( !file && ( flags & IMGFLAG_MIPMAP ) && mips > 1 && vk.active && !R_StreamWholeWanted( fileName, flags ) ) {
		const int from = skip;
		const ddsFit_t fit = { vk_format_block_bytes( format ), w, h, mips };
		skip = vk_vram_fit( w, h, skip, mips - 1, R_DDSFitBytes, &fit );
		if ( skip != from )
			vk_vram_degraded( ddsName, MAX( w >> from, 1 ), MAX( h >> from, 1 ), MAX( w >> skip, 1 ), MAX( h >> skip, 1 ) );
	}
	if ( skip > mips - 1 )
		skip = mips - 1;
	if ( ( MAX( w, h ) >> skip ) > (int)deviceMax ) {	// too few stored levels to drop below the device limit
		ri.Printf( PRINT_DEVELOPER, "DDS: %s: %ix%i with %i levels exceeds the device limit %u, using the other loaders\n", ddsName, w, h, mips, deviceMax );
		DDS_FAIL();
	}
	n = ( flags & IMGFLAG_MIPMAP ) ? mips - skip : 1;
	if ( n > 16 )
		n = 16;   // vk_upload_image_data copies at most 16 levels

	blockBytes = vk_format_block_bytes( format );
	levelBytes = 0;
	for ( i = 0; i < skip + n; i++ ) {
		const int lw = MAX( w >> i, 1 ), lh = MAX( h >> i, 1 );
		const int bytes = vk_block_level_size( blockBytes, lw, lh );
		if ( i < skip )
			dataOffset += bytes;
		else {
			levelOffset[i - skip] = dataOffset + levelBytes;
			levelBytes += bytes;
		}
	}
	if ( dataOffset + levelBytes > len ) {
		ri.Printf( PRINT_WARNING, "DDS: %s: %i bytes, needs %i, using the other loaders\n", ddsName, len, dataOffset + levelBytes );
		DDS_FAIL();
	}
#undef DDS_FAIL

	w = MAX( w >> skip, 1 );
	h = MAX( h >> skip, 1 );
	tailLevel = 0;
	if ( file ) {
		// the tail streams from; a texture that is all tail is read whole by range (rangeOnly: no tail, just its levels)
		tailLevel = rangeOnly ? 0 : R_StreamTailLevel( fileName, w, h, n );
		const int from = levelOffset[tailLevel];
		const int bytes = dataOffset + levelBytes - from;
		data = DDS_AllocRange( bytes, &dataPages );
		if ( ri.FS_RangeRead( file, from, bytes, data ) < 0 ) {
			DDS_FreeRange( data, dataPages );
			ri.FS_RangeClose( file );
			ri.Printf( PRINT_WARNING, "DDS: %s: range read failed, reading the whole file\n", ddsName );
			return R_LoadDDSImageFrom( imageName, fileName, flags, type, qfalse );
		}
		if ( !tailLevel ) {
			ri.FS_RangeClose( file );
			file = 0;
		}
	} else if ( n > 1 && R_StreamWholeWanted( fileName, flags ) ) {
		// not readable by range (deflated in its pk3, or loose): whole-loaded, held in the stream pool (r_streamWhole)
		tailLevel = R_StreamTailLevel( fileName, w, h, n );
	}

	image = R_NewImage( imageName, w, h, flags, format, type );
	image->uploadWidth = w;
	image->uploadHeight = h;
	image->layers = 1;
	image->mipLevels = n;
	if ( tailLevel && file ) {
		const int tw = MAX( w >> tailLevel, 1 ), th = MAX( h >> tailLevel, 1 );
		R_StreamAttach( image, file, ddsName, len, w, h, n, tailLevel, blockBytes, levelOffset );
		vk_create_image( image, tw, th, n - tailLevel );
		vk_upload_image_data( image, 0, 0, tw, th, n - tailLevel, data, dataOffset + levelBytes - levelOffset[tailLevel], qfalse );
		R_StreamTailReady( image );
	} else if ( tailLevel ) {
		const int tw = MAX( w >> tailLevel, 1 ), th = MAX( h >> tailLevel, 1 );
		R_StreamAttachWhole( image, qfalse, ddsName, w, h, n, tailLevel, blockBytes, levelOffset );
		vk_create_image( image, tw, th, n - tailLevel );
		vk_upload_image_data( image, 0, 0, tw, th, n - tailLevel, b + levelOffset[tailLevel], dataOffset + levelBytes - levelOffset[tailLevel], qfalse );
		R_StreamTailReady( image );
		R_StreamWholeLoaded( image, b + levelOffset[0], levelBytes );
	} else {
		vk_create_image( image, w, h, n );
		vk_upload_image_data( image, 0, 0, w, h, n, data ? data : b + dataOffset, levelBytes, qfalse );
	}
	if ( data )
		DDS_FreeRange( data, dataPages );
	else
		ri.FS_FreeFile( b );
	if ( tProf0 > 0.0 ) {
		const double total = R_SunProfNow() - tProf0;
		if ( total > 5.0 )
			ri.Printf( PRINT_ALL, "LOADIMG %s %.1f ms: whole-file read %.1f ms (%d bytes), %dx%d %d levels, skip %d, %s\n", ddsName, total,
				tProfRead, tProfRead > 0.0 ? len : 0, w, h, n, skip, tailLevel ? "streamed tail" : "whole" );
	}
	return image;
}

image_t *R_LoadDDSImage( const char *imageName, const char *fileName, imgFlags_t flags, uint32_t type ) {
	return R_LoadDDSImageFrom( imageName, fileName, flags, type, qtrue );
}

/*
================
R_FindImageArray

A 2D array image from count files (layered materials), built once per name. Every layer is resampled to the largest
file's power-of-two size, at most PBR_LAYER_MAX_SIZE. A missing file entry or "-" is a neutral layer: white, or for
normals a flat normal with zero metalness. NULL when a named file cannot be loaded.
================
*/
// the layers' pixels, each resampled to the largest file's power-of-two size (at most PBR_LAYER_MAX_SIZE): Z_Malloc'd,
// freed by the caller. qfalse when a named file cannot be loaded
static qboolean R_LoadArrayLayers( const char **files, int count, qboolean normals, byte **layers, int *outW, int *outH )
{
	byte		*pics[PBR_MAX_LAYERS] = { NULL };
	int			widths[PBR_MAX_LAYERS], heights[PBR_MAX_LAYERS];
	int			i, width = 1, height = 1, w, h;

	for ( i = 0; i < count; i++ ) {
		widths[i] = heights[i] = 0;
		if ( !files || !files[i] || !files[i][0] || !strcmp( files[i], "-" ) )
			continue;
		R_LoadImage( files[i], &pics[i], &widths[i], &heights[i] );
		if ( !pics[i] ) {
			ri.Printf( PRINT_WARNING, "WARNING: could not load layer image '%s'\n", files[i] );
			while ( --i >= 0 )
				if ( pics[i] )
					ri.Z_Free( pics[i] );
			return qfalse;
		}
		width = MAX( width, widths[i] );
		height = MAX( height, heights[i] );
	}
	for ( w = 1; w < width && w < PBR_LAYER_MAX_SIZE; w <<= 1 );
	for ( h = 1; h < height && h < PBR_LAYER_MAX_SIZE; h <<= 1 );

	for ( i = 0; i < count; i++ ) {
		layers[i] = (byte *)Z_Malloc( w * h * 4, TAG_TEMP_IMAGE );
		if ( pics[i] && widths[i] == w && heights[i] == h ) {
			Com_Memcpy( layers[i], pics[i], w * h * 4 );
		} else if ( pics[i] ) {
			ResampleTexture( (unsigned *)pics[i], widths[i], heights[i], (unsigned *)layers[i], w, h );
		} else {
			for ( int p = 0; p < w * h; p++ ) {
				byte *px = layers[i] + p * 4;
				px[0] = px[1] = normals ? 128 : 255;
				px[2] = normals ? 0 : 255;
				px[3] = 255;
			}
		}
		if ( pics[i] )
			ri.Z_Free( pics[i] );
	}
	*outW = w;
	*outH = h;
	return qtrue;
}

// every layer's levels from level on into image->handle, which was created for them: each layer's own chain
// (vk_generate_image_upload_data: the same size and format for all, IMGFLAG_ARRAY is RGBA8)
static void R_UploadArrayLevels( image_t *image, byte **layers, int count, int level )
{
	for ( int i = 0; i < count; i++ ) {
		Image_Upload_Data upload_data;
		int offset = 0, w, h;

		image->internalFormat = 0;	// chosen again for each layer (the same)
		vk_generate_image_upload_data( image, layers[i], &upload_data );
		w = upload_data.base_level_width;
		h = upload_data.base_level_height;
		for ( int k = 0; k < level; k++ )
			offset += MAX( w >> k, 1 ) * MAX( h >> k, 1 ) * 4;
		image->uploadLayer = i;
		vk_upload_image_data( image, 0, 0, MAX( w >> level, 1 ), MAX( h >> level, 1 ), upload_data.mip_levels - level,
			upload_data.buffer + offset, upload_data.buffer_size - offset, i > 0 ? qtrue : qfalse );
	}
	image->uploadLayer = 0;
}

// vk_stream.cpp: a streamed array's levels from level on into image->handle (a build made for them); pixels: its
// layers' pixels, or NULL to decode its files again (a reload)
qboolean R_ImageArrayUpload( image_t *image, const char (*files)[MAX_QPATH], int count, qboolean normals, byte **pixels, int level )
{
	byte		*own[PBR_MAX_LAYERS] = { NULL };
	const char	*names[PBR_MAX_LAYERS];
	int			i, w = 0, h = 0;

	count = count < 1 ? 1 : count > PBR_MAX_LAYERS ? PBR_MAX_LAYERS : count;
	if ( !pixels ) {
		for ( i = 0; i < count; i++ )
			names[i] = files[i];
		if ( !R_LoadArrayLayers( names, count, normals, own, &w, &h ) )
			return qfalse;
		if ( w != image->width || h != image->height ) {
			for ( i = 0; i < count; i++ )
				ri.Z_Free( own[i] );
			return qfalse;
		}
		pixels = own;
	}
	R_UploadArrayLevels( image, pixels, count, level );
	for ( i = 0; i < count; i++ )
		if ( own[i] )
			ri.Z_Free( own[i] );
	return qtrue;
}

// a layer array from block-compressed DDS files beside its layer images (r_ddsTextures): every layer must
// have one, all of one format, size and level count; levels past PBR_LAYER_MAX_SIZE (and r_ddsMaxSize) are dropped from
// the top, as the RGBA8 path resamples to it. Uploaded whole: a BC7 array is a quarter of the RGBA8 one, so it is not
// held in the stream pool. NULL: the RGBA8 path (a layer without a DDS, a mismatch, an unsampled format).
static double s_arrayReadMs;	// r_loadProfile above 1: the layer files' read time inside the array being timed (LOADARR)
static uint64_t s_arrayHash;	// r_loadProfile 3: FNV-1a of the array's uploaded bytes, every layer and level (LOADARR)

static void R_ArrayHash( const byte *p, int n ) {
	uint64_t h = s_arrayHash;
	for ( int i = 0; i < n; i++ ) {
		h ^= p[i];
		h *= 1099511628211ULL;
	}
	s_arrayHash = h;
}

// a layer DDS may also be uncompressed RGBA8 (DX10, DXGI 28), for the
// arrays whose layers BC7 cannot hold; one level's bytes by format
static int R_ArrayLevelSize( VkFormat format, int width, int height ) {
	if ( format == VK_FORMAT_R8G8B8A8_UNORM )
		return width * height * 4;
	return vk_block_level_size( vk_format_block_bytes( format ), width, height );
}

// r_arrayDump 1: an RGBA8 layer as uploaded (its whole chain) to arraydump/<layer file>.dds, DX10 RGBA8, for the pack tool
static void R_ArrayDumpLayer( const char *file, const byte *data, int bytes, int w, int h, int mips ) {
	char out[MAX_QPATH + 16];
	byte hdr[148];
	uint32_t u[37];
	Com_Memset( u, 0, sizeof( u ) );
	memcpy( &u[0], "DDS ", 4 );
	u[1] = 124;
	u[2] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8;	// caps, height, width, pixelformat, mipmapcount, pitch
	u[3] = h;
	u[4] = w;
	u[5] = w * 4;
	u[7] = mips;
	u[19] = 32;			// pixel format: size
	u[20] = 0x4;		// DDPF_FOURCC
	memcpy( &u[21], "DX10", 4 );
	u[27] = 0x1000 | 0x400000 | 0x8;	// texture, mipmap, complex
	u[32] = 28;			// DXGI_FORMAT_R8G8B8A8_UNORM
	u[33] = 3;			// TEXTURE2D
	u[35] = 1;			// array size
	memcpy( hdr, u, sizeof( hdr ) );
	byte *buf = (byte *)ri.Hunk_AllocateTempMemory( (int)sizeof( hdr ) + bytes );
	memcpy( buf, hdr, sizeof( hdr ) );
	memcpy( buf + sizeof( hdr ), data, bytes );
	COM_StripExtension( file, out, sizeof( out ) );
	ri.FS_WriteFile( va( "arraydump/%s.dds", out ), buf, (int)sizeof( hdr ) + bytes );
	ri.Hunk_FreeTempMemory( buf );
}

static image_t *R_LoadArrayDDS( const char *name, const char **files, int count, imgFlags_t flags )
{
	byte		*file[PBR_MAX_LAYERS] = { NULL };
	int			len[PBR_MAX_LAYERS], offset[PBR_MAX_LAYERS];
	VkFormat	format = VK_FORMAT_UNDEFINED;
	int			i, w = 0, h = 0, mips = 0, skip = 0, n, levelBytes, skipBytes;
	image_t		*image = NULL;
	char		ddsName[MAX_QPATH + 8];

	if ( !r_ddsTextures || !r_ddsTextures->integer || !files || count < 1 )
		return NULL;
	for ( i = 0; i < count; i++ ) {
		int lw, lh, lm, dataOffset;
		VkFormat f;
		if ( !files[i] || !files[i][0] || !strcmp( files[i], "-" ) )
			goto done;
		COM_StripExtension( files[i], ddsName, sizeof( ddsName ) );
		Q_strcat( ddsName, sizeof( ddsName ), ".dds" );
		const double tRead0 = R_SunProfNow();
		len[i] = ri.FS_ReadFile( ddsName, (void **)&file[i] );
		s_arrayReadMs += R_SunProfNow() - tRead0;	// r_loadProfile: LOADARR
		if ( len[i] < 128 || !file[i] || memcmp( file[i], "DDS ", 4 ) )
			goto done;
		if ( len[i] >= 148 && !memcmp( file[i] + 84, "DX10", 4 ) && ( file[i][128] | ( file[i][129] << 8 ) ) == 28 ) {
			f = VK_FORMAT_R8G8B8A8_UNORM;	// an uncompressed layer (lossless)
			dataOffset = 148;
		} else
			f = R_DDSFormat( file[i], len[i], &dataOffset );
		lh = file[i][12] | ( file[i][13] << 8 ) | ( file[i][14] << 16 ) | ( file[i][15] << 24 );
		lw = file[i][16] | ( file[i][17] << 8 ) | ( file[i][18] << 16 ) | ( file[i][19] << 24 );
		lm = file[i][28] | ( file[i][29] << 8 ) | ( file[i][30] << 16 ) | ( file[i][31] << 24 );
		if ( lm < 1 )
			lm = 1;
		if ( f == VK_FORMAT_UNDEFINED || lw < 4 || lh < 4 || ( lw & ( lw - 1 ) ) || ( lh & ( lh - 1 ) ) || lw > DDS_MAX_SIDE || lh > DDS_MAX_SIDE )
			goto done;
		if ( i == 0 ) {
			VkFormatProperties props;
			qvkGetPhysicalDeviceFormatProperties( vk.physical_device, f, &props );
			if ( !( props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT ) )
				goto done;
			format = f; w = lw; h = lh; mips = lm;
		} else if ( f != format || lw != w || lh != h || lm != mips ) {
			ri.Printf( PRINT_DEVELOPER, "layer array %s: %s differs from the first layer's DDS, using the images\n", name, ddsName );
			goto done;
		}
		offset[i] = dataOffset;
	}
	{	// no more levels than the extent allows; the top ones past the layer size limit dropped
		int chain = 1, limit = PBR_LAYER_MAX_SIZE;
		for ( int d = MAX( w, h ); d > 1; d >>= 1 ) chain++;
		if ( mips > chain )
			mips = chain;
		if ( r_ddsMaxSize->integer > 0 && r_ddsMaxSize->integer < limit )
			limit = r_ddsMaxSize->integer;
		while ( ( MAX( w, h ) >> skip ) > limit && skip < mips - 1 )
			skip++;
		VkPhysicalDeviceProperties dp;
		qvkGetPhysicalDeviceProperties( vk.physical_device, &dp );
		if ( ( MAX( w, h ) >> skip ) > (int)dp.limits.maxImageDimension2D )
			goto done;	// too few stored levels to drop below the device limit: the RGBA8 path resamples
	}
	n = ( flags & IMGFLAG_MIPMAP ) ? mips - skip : 1;
	if ( n > 16 )
		n = 16;
	skipBytes = levelBytes = 0;
	for ( i = 0; i < skip + n; i++ ) {
		const int bytes = R_ArrayLevelSize( format, MAX( w >> i, 1 ), MAX( h >> i, 1 ) );
		if ( i < skip ) skipBytes += bytes; else levelBytes += bytes;
	}
	for ( i = 0; i < count; i++ )
		if ( offset[i] + skipBytes + levelBytes > len[i] )
			goto done;
	w = MAX( w >> skip, 1 );
	h = MAX( h >> skip, 1 );

	image = R_NewImage( name, w, h, flags | IMGFLAG_ARRAY, format, 0 );
	image->flags = flags | IMGFLAG_ARRAY;
	image->uploadWidth = w;
	image->uploadHeight = h;
	image->wrapClampMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;	// arrays repeat whatever the flags say
	image->layers = count;
	image->mipLevels = n;
	vk_create_image( image, w, h, n );
	for ( i = 0; i < count; i++ ) {
		image->uploadLayer = i;
		// layers after the first keep the image's contents (the transition covers every layer: from UNDEFINED it may
		// discard the layers already written), as the RGBA8 path does
		vk_upload_image_data( image, 0, 0, w, h, n, file[i] + offset[i] + skipBytes, levelBytes, i > 0 ? qtrue : qfalse );
		if ( r_loadProfile->integer > 2 )
			R_ArrayHash( file[i] + offset[i] + skipBytes, levelBytes );
	}
	image->uploadLayer = 0;
	ri.Printf( PRINT_DEVELOPER, "layer array %s: %i layers %ix%i, %i levels, from DDS (format %i)\n", name, count, w, h, n, (int)format );
done:
	for ( i = 0; i < count; i++ )
		if ( file[i] )
			ri.FS_FreeFile( file[i] );
	return image;
}

image_t *R_FindImageArray( const char *name, const char **files, int count, imgFlags_t flags, qboolean normals, float wantScale )
{
	byte		*layers[PBR_MAX_LAYERS] = { NULL };
	int			i, w, h, tailLevel = 0;
	image_t		*image;
	const int	namelen = (int)strlen( name ) + 1;
	const char	*first = NULL;

	if ( ( image = R_ImageByName( name ) ) != NULL ) {
		R_StreamArrayScale( image, wantScale );
		return image;
	}
	if ( namelen > MAX_QPATH )
		return NULL;

	count = count < 1 ? 1 : count > PBR_MAX_LAYERS ? PBR_MAX_LAYERS : count;
	// r_loadProfile above 1: LOADARR for an array whose load takes over 5 ms, from DDS or RGBA8, its reads
	const double tArr0 = R_SunProfNow();
	s_arrayReadMs = 0.0;
	s_arrayHash = 14695981039346656037ULL;
	if ( r_arrayDDS->integer && ( image = R_LoadArrayDDS( name, files, count, flags ) ) != NULL ) {	// r_arrayDDS 0: test
		const double ms = R_SunProfNow() - tArr0;
		tr_loadProf[LOADPROF_ARRAYS] += ms;
		if ( r_loadProfile->integer > 2 || ( r_loadProfile->integer > 1 && ms > 5.0 ) )
			ri.Printf( PRINT_ALL, "LOADARR %s %.1f ms: DDS, %d layers %dx%d, file reads %.1f ms, format %d, hash %016llx\n",
				files && files[0] ? files[0] : name, ms, count, image->uploadWidth, image->uploadHeight, s_arrayReadMs,
				(int)image->internalFormat, (unsigned long long)s_arrayHash );
		return image;
	}
	const double tLayers0 = R_SunProfNow();
	if ( !R_LoadArrayLayers( files, count, normals, layers, &w, &h ) )
		return NULL;
	const double layersMs = R_SunProfNow() - tLayers0;
	tr_loadProf[LOADPROF_ARRAYS] += R_SunProfNow() - tArr0;
	if ( r_loadProfile->integer > 1 && layersMs > 5.0 )
		ri.Printf( PRINT_ALL, "LOADARR %s %.1f ms: RGBA8 layers decoded and resampled, %d layers\n", files && files[0] ? files[0] : name,
			layersMs, count );

	image = R_NewImage( name, w, h, flags | IMGFLAG_ARRAY | IMGFLAG_NO_COMPRESSION, 0, 0 );
	image->flags = flags | IMGFLAG_ARRAY | IMGFLAG_NO_COMPRESSION;
	image->wrapClampMode = VK_SAMPLER_ADDRESS_MODE_REPEAT;	// arrays repeat whatever the flags say

	// every layer gets the same size and format: layer 0's chain says them
	{
		Image_Upload_Data upload_data;

		image->internalFormat = 0;
		vk_generate_image_upload_data( image, layers[0], &upload_data );
		image->uploadWidth = upload_data.base_level_width;
		image->uploadHeight = upload_data.base_level_height;
		image->layers = count;
		image->mipLevels = upload_data.mip_levels;
	}
	// r_loadProfile 3: the uploaded bytes' hash (every layer's whole chain, as R_UploadArrayLevels makes them); r_arrayDump 1:
	// the same bytes written per layer as RGBA8 DDS (the lossless layer files)
	if ( r_loadProfile->integer > 2 || r_arrayDump->integer ) {
		for ( i = 0; i < count; i++ ) {
			Image_Upload_Data up;
			image->internalFormat = 0;
			vk_generate_image_upload_data( image, layers[i], &up );
			if ( r_loadProfile->integer > 2 )
				R_ArrayHash( up.buffer, up.buffer_size );
			if ( r_arrayDump->integer && files && files[i] && files[i][0] && strcmp( files[i], "-" ) )
				R_ArrayDumpLayer( files[i], up.buffer, up.buffer_size, up.base_level_width, up.base_level_height, up.mip_levels );
		}
		if ( r_loadProfile->integer > 2 )
			ri.Printf( PRINT_ALL, "LOADARR %s %.1f ms: RGBA8 path, %d layers %dx%d, hash %016llx\n", files && files[0] ? files[0] : name,
				layersMs, count, image->uploadWidth, image->uploadHeight, (unsigned long long)s_arrayHash );
	}
	// xenjk: a character's array is held in the stream pool (vk_stream.cpp, r_streamWhole), its tail here
	for ( i = 0; files && i < count && !first; i++ )
		if ( files[i] && files[i][0] && strcmp( files[i], "-" ) )
			first = files[i];
	if ( wantScale > 0.0f && first && R_StreamWholeWanted( first, flags ) && vk.active && image->internalFormat == VK_FORMAT_R8G8B8A8_UNORM
		&& image->mipLevels > 1 && image->mipLevels <= 16 )
		tailLevel = R_StreamTailLevel( first, image->uploadWidth, image->uploadHeight, image->mipLevels );
	if ( tailLevel ) {
		R_StreamAttachArray( image, files, count, normals, image->uploadWidth, image->uploadHeight, image->mipLevels, tailLevel, wantScale );
		vk_create_image( image, MAX( image->uploadWidth >> tailLevel, 1 ), MAX( image->uploadHeight >> tailLevel, 1 ), image->mipLevels - tailLevel );
		R_UploadArrayLevels( image, layers, count, tailLevel );
		R_StreamTailReady( image );
		R_StreamArrayLoaded( image, layers );
	} else {
		vk_create_image( image, image->uploadWidth, image->uploadHeight, image->mipLevels );
		R_UploadArrayLevels( image, layers, count, 0 );
	}
	for ( i = 0; i < count; i++ )
		ri.Z_Free( layers[i] );

	return image;
}
