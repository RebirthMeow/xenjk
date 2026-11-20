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
Video capture: deferred readback.

vk_read_pixels waits for the GPU to go idle, allocates a host image, submits a one-off copy, waits again and frees
it, every frame. Here vk_end_frame records the copy into the frame's own command buffer, into a host-visible buffer
owned by that command-buffer slot, and the frame is encoded once the slot's fence has signalled: at the slot's next
vk_begin_frame, or at a flush (the client asks before it closes the file). The pixels, conversion and encoder are
the immediate path's (vk_convert_capture_pixels, RB_EncodeVideoFrame); only the timing differs. r_videoAsync 0
keeps the immediate path.
*/

#include "tr_local.h"

qboolean vk_video_client_flushes;	// the client took the hooks (GetRefAPI) and flushes before it frees its buffers

typedef struct {
	VkBuffer				buffer;
	VkDeviceMemory			memory;
	byte					*mapped;
	qboolean				pending;
	uint32_t				seq;
	videoFrameCommand_t		cmd;
} vkVideoSlot_t;

static struct {
	vkVideoSlot_t			slot[NUM_COMMAND_BUFFERS];
	qboolean				coherent;
	uint32_t				width, height, pixelWidth;
	uint32_t				seq;

	// capture-format copy of the color image when its format differs (the immediate path blits for the same reason)
	VkImage					image;
	VkDeviceMemory			imageMemory;
} vv;

static uint32_t vk_video_pixel_width( void )
{
	switch ( vk.capture_format ) {
		case VK_FORMAT_B4G4R4A4_UNORM_PACK16: return 2;
		case VK_FORMAT_R16G16B16A16_UNORM: return 8;
		case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
		default: return 4;
	}
}

// the image the immediate path reads, and the layout it is in when the frame's passes have ended
static VkImage vk_video_source( VkImageLayout *layout, VkFormat *format )
{
	if ( vk.capture.image ) {
		*layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		*format = vk.capture_format;
		return vk.capture.image;
	}
	*layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	*format = vk.color_format;
	return vk.color_image;
}

static void vk_video_free( void )
{
	uint32_t i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vkVideoSlot_t *s = &vv.slot[i];
		if ( s->mapped )
			qvkUnmapMemory( vk.device, s->memory );
		if ( s->buffer != VK_NULL_HANDLE )
			qvkDestroyBuffer( vk.device, s->buffer, NULL );
		if ( s->memory != VK_NULL_HANDLE )
			VK_FREE_MEMORY( vk.device, s->memory );
	}
	if ( vv.image != VK_NULL_HANDLE )
		qvkDestroyImage( vk.device, vv.image, NULL );
	if ( vv.imageMemory != VK_NULL_HANDLE )
		VK_FREE_MEMORY( vk.device, vv.imageMemory );

	Com_Memset( &vv, 0, sizeof( vv ) );
}

static qboolean vk_video_alloc( uint32_t width, uint32_t height, qboolean convert )
{
	VkMemoryRequirements req;
	VkMemoryPropertyFlags flags;
	VkMemoryAllocateInfo alloc_info;
	uint32_t i;

	vv.width = width;
	vv.height = height;
	vv.pixelWidth = vk_video_pixel_width();

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		vkVideoSlot_t *s = &vv.slot[i];
		VkBufferCreateInfo desc;

		Com_Memset( &desc, 0, sizeof( desc ) );
		desc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		desc.size = (VkDeviceSize)width * height * vv.pixelWidth;
		desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK( qvkCreateBuffer( vk.device, &desc, NULL, &s->buffer ) );

		qvkGetBufferMemoryRequirements( vk.device, s->buffer, &req );

		// host-cached memory reads fast; coherent spares the invalidate (same preference order as vk_read_pixels)
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = vk_find_memory_type_lazy( req.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &flags );
		if ( alloc_info.memoryTypeIndex == ~0U )
			alloc_info.memoryTypeIndex = vk_find_memory_type_lazy( req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, &flags );
		if ( alloc_info.memoryTypeIndex == ~0U )
			alloc_info.memoryTypeIndex = vk_find_memory_type_lazy( req.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &flags );
		if ( alloc_info.memoryTypeIndex == ~0U ) {
			ri.Printf( PRINT_WARNING, "video capture: no host-visible memory for the readback ring; reading at once\n" );
			vk_video_free();
			return qfalse;
		}
		vv.coherent = ( flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) ? qtrue : qfalse;

		VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc_info, &s->memory, "video capture ring" );
		VK_CHECK( qvkBindBufferMemory( vk.device, s->buffer, s->memory, 0 ) );
		VK_CHECK( qvkMapMemory( vk.device, s->memory, 0, VK_WHOLE_SIZE, 0, (void**)&s->mapped ) );
	}

	if ( convert ) {
		VkImageCreateInfo desc;

		Com_Memset( &desc, 0, sizeof( desc ) );
		desc.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		desc.imageType = VK_IMAGE_TYPE_2D;
		desc.format = vk.capture_format;
		desc.extent.width = width;
		desc.extent.height = height;
		desc.extent.depth = 1;
		desc.mipLevels = 1;
		desc.arrayLayers = 1;
		desc.samples = VK_SAMPLE_COUNT_1_BIT;
		desc.tiling = VK_IMAGE_TILING_OPTIMAL;
		desc.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		desc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK( qvkCreateImage( vk.device, &desc, NULL, &vv.image ) );

		qvkGetImageMemoryRequirements( vk.device, vv.image, &req );
		alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		alloc_info.pNext = NULL;
		alloc_info.allocationSize = req.size;
		alloc_info.memoryTypeIndex = vk_find_memory_type_lazy( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &flags );
		if ( alloc_info.memoryTypeIndex == ~0U ) {
			vk_video_free();
			return qfalse;
		}
		VK_ALLOCATE_MEMORY_CHECK( vk.device, &alloc_info, &vv.imageMemory, "video capture convert" );
		VK_CHECK( qvkBindImageMemory( vk.device, vv.image, vv.imageMemory, 0 ) );
	}

	return qtrue;
}

static void vk_video_deliver( vkVideoSlot_t *s )
{
	if ( !s->pending )
		return;
	s->pending = qfalse;

	if ( !vv.coherent ) {
		VkMappedMemoryRange range;
		range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		range.pNext = NULL;
		range.memory = s->memory;
		range.offset = 0;
		range.size = VK_WHOLE_SIZE;
		qvkInvalidateMappedMemoryRanges( vk.device, 1, &range );
	}

	vk_convert_capture_pixels( s->mapped, vv.width * vv.pixelWidth, vv.width, vv.height, (byte*)PADP( s->cmd.captureBuffer, 1 ) );
	RB_EncodeVideoFrame( &s->cmd );
}

/*
=============
vk_video_record

Called by vk_end_frame after the frame's last pass, before the command buffer ends. Returns qfalse to leave the
request to the immediate path (r_videoAsync 0, a client without the hooks, which frees its buffers without a flush,
a size the client did not ask for, no suitable memory).
=============
*/
qboolean vk_video_record( void )
{
	const videoFrameCommand_t *cmd = &backEnd.vcmd;
	VkCommandBuffer cb = vk.cmd->command_buffer;
	vkVideoSlot_t *s;
	VkImageLayout srcLayout;
	VkFormat srcFormat;
	VkImage src, copySrc;
	VkBufferImageCopy region;
	VkBufferMemoryBarrier barrier;
	qboolean convert;

	// the immediate path encodes this frame now: earlier frames still in the ring (r_videoAsync just turned off) go first
	if ( !r_videoAsync->integer || !vk_video_client_flushes || !cmd->captureBuffer ) {
		vk_video_flush();
		return qfalse;
	}

	// the client sizes its buffers from GetCaptureSize; anything else (an older client) reads at once as before
	if ( cmd->width != (int)gls.captureWidth || cmd->height != (int)gls.captureHeight ) {
		vk_video_flush();
		return qfalse;
	}

	src = vk_video_source( &srcLayout, &srcFormat );
	convert = ( srcFormat != vk.capture_format ) ? qtrue : qfalse;

	if ( vv.width != gls.captureWidth || vv.height != gls.captureHeight || ( vv.image != VK_NULL_HANDLE ) != convert
		|| vv.pixelWidth != vk_video_pixel_width() ) {
		// a ring that could not be made is not tried (and warned about) again until the capture size or format changes
		static uint32_t failedKey[4];
		static qboolean failed;
		const uint32_t key[4] = { (uint32_t)gls.captureWidth, (uint32_t)gls.captureHeight, (uint32_t)convert,
			vk_video_pixel_width() };
		if ( failed && !memcmp( key, failedKey, sizeof( key ) ) )
			return qfalse;
		vk_video_flush();
		vk_video_free();
		failed = qfalse;
		if ( !vk_video_alloc( gls.captureWidth, gls.captureHeight, convert ) ) {
			memcpy( failedKey, key, sizeof( key ) );
			failed = qtrue;
			return qfalse;
		}
	}

	s = &vv.slot[vk.cmd_index];
	if ( s->pending )	// not delivered at this slot's begin_frame (cannot happen while the fence is waited there)
		return qfalse;

	if ( srcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ) {
		vk_record_image_layout_transition( cb, src, VK_IMAGE_ASPECT_COLOR_BIT, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
	} else {
		// the capture pass ends in TRANSFER_SRC_OPTIMAL but its external dependency only covers fragment-shader reads:
		// make its colour writes visible to the transfer
		VkImageMemoryBarrier written;

		Com_Memset( &written, 0, sizeof( written ) );
		written.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		written.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		written.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		written.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		written.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		written.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		written.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		written.image = src;
		written.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		written.subresourceRange.levelCount = 1;
		written.subresourceRange.layerCount = 1;
		qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &written );
	}

	copySrc = src;
	if ( convert ) {
		VkImageBlit blit;

		vk_record_image_layout_transition( cb, vv.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0 );

		Com_Memset( &blit, 0, sizeof( blit ) );
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[1].x = vv.width;
		blit.srcOffsets[1].y = vv.height;
		blit.srcOffsets[1].z = 1;
		blit.dstSubresource = blit.srcSubresource;
		blit.dstOffsets[1] = blit.srcOffsets[1];
		qvkCmdBlitImage( cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vv.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_NEAREST );

		vk_record_image_layout_transition( cb, vv.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0 );
		copySrc = vv.image;
	}

	Com_Memset( &region, 0, sizeof( region ) );
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.width = vv.width;
	region.imageExtent.height = vv.height;
	region.imageExtent.depth = 1;
	qvkCmdCopyImageToBuffer( cb, copySrc, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->buffer, 1, &region );

	// the host reads the buffer after the frame's fence
	Com_Memset( &barrier, 0, sizeof( barrier ) );
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = s->buffer;
	barrier.size = VK_WHOLE_SIZE;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL );

	if ( srcLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL )
		vk_record_image_layout_transition( cb, src, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, 0, 0 );

	s->cmd = *cmd;
	s->seq = ++vv.seq;
	s->pending = qtrue;
	return qtrue;
}

// vk_begin_frame, after the slot's fence: that slot's capture is complete
void vk_video_begin_frame( void )
{
	vk_video_deliver( &vv.slot[vk.cmd_index] );
}

/*
=============
vk_video_flush

Delivers every pending capture, oldest first, waiting for each frame's fence. The fence is left signalled;
vk_begin_frame waits on it again, returns at once and resets it.
=============
*/
void vk_video_flush( void )
{
	for ( ;; ) {
		vkVideoSlot_t *oldest = NULL;
		uint32_t i, index = 0;

		for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			if ( vv.slot[i].pending && ( !oldest || vv.slot[i].seq < oldest->seq ) ) {
				oldest = &vv.slot[i];
				index = i;
			}
		}
		if ( !oldest )
			return;

		if ( vk.tess[index].waitForFence ) {
			VkResult res = qvkWaitForFences( vk.device, 1, &vk.tess[index].rendering_finished_fence, VK_FALSE, 1e10 );
			if ( res != VK_SUCCESS ) {
				ri.Printf( PRINT_WARNING, "video capture: %s returned %s; frame dropped\n", "vkWaitForFences", vk_result_string( res ) );
				oldest->pending = qfalse;
				continue;
			}
		}
		vk_video_deliver( oldest );
	}
}

void vk_video_capture_size( int *width, int *height )
{
	*width = gls.captureWidth;
	*height = gls.captureHeight;
}

// vk_shutdown: captures still pending are dropped (the client flushes before it closes a file)
void vk_video_shutdown( void )
{
	uint32_t i;

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ )
		vv.slot[i].pending = qfalse;

	if ( vv.width || vv.image != VK_NULL_HANDLE ) {
		qvkDeviceWaitIdle( vk.device );
		vk_video_free();
	}
}
