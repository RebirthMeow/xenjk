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
Buffer uploads that do not wait for the GPU (docs/character-loading-plan.md, M5 mesh steps).

The one-shot path (vk_begin_command_buffer / vk_end_command_buffer) waits for the queue to go idle after every copy:
with a frame in flight that is the rest of that frame's GPU time, per buffer (a player model's LODs, eight to ten
waits). vk_upload_buffer copies the data into a persistently mapped staging ring and records the copy into a batch
command buffer of its own. vk_upload_flush submits the batch, ending in a barrier whose second scope covers every
later submission; it runs before every other submission that could read the buffers: the frame's (vk_end_frame),
the render scene's mid-frame one (vk_scene.cpp), a one-shot command buffer's (vk_end_command_buffer) and
vk_wait_idle. Each batch has a fence; ring space returns when its batch's fence has signalled. A full ring waits for
the batches in flight (that empties it, so a piece, at most a quarter of the ring, then always fits).
*/
#include "tr_local.h"

#include <vector>

#define UPLOAD_RING		( 64u << 20 )	// staging ring bytes
#define UPLOAD_PIECE	( 16u << 20 )	// largest ring allocation (a larger upload goes in pieces)
#define UPLOAD_BATCHES	8
#define UPLOAD_ALIGN	256u

struct uploadBatch_t {
	VkCommandBuffer	cmd;
	VkFence			fence;
	uint64_t		id;			// batch number, 0 = slot never used
	bool			submitted;
};

struct uploadSeg_t {
	uint32_t	start, end;
	uint64_t	batch;
};

static struct {
	bool						ready;
	VkCommandPool				pool;
	uploadBatch_t				batches[UPLOAD_BATCHES];
	uint64_t					nextBatch;		// the batch being recorded, once recording
	bool						recording;
	VkBuffer					ring;
	VkDeviceMemory				ringMemory;
	byte						*ringPtr;
	uint32_t					head;
	std::vector<uploadSeg_t>	segs;			// oldest first
	uint64_t					bytes, uploads;
} up;

static bool vk_upload_init( void ) {
	if ( up.ready ) {
		return true;
	}
	if ( !vk.device ) {
		return false;
	}
	VkBufferCreateInfo bi;
	Com_Memset( &bi, 0, sizeof( bi ) );
	bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bi.size = UPLOAD_RING;
	bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CREATE_BUFFER( vk.device, &bi, &up.ring, "upload ring" );
	VkMemoryRequirements req;
	qvkGetBufferMemoryRequirements( vk.device, up.ring, &req );
	VkMemoryAllocateInfo ai;
	Com_Memset( &ai, 0, sizeof( ai ) );
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	if ( VK_ALLOCATE_MEMORY( vk.device, &ai, &up.ringMemory, "upload ring" ) != VK_SUCCESS ) {
		VK_DESTROY_BUFFER( vk.device, up.ring );
		up.ring = VK_NULL_HANDLE;
		up.ringMemory = VK_NULL_HANDLE;
		return false;
	}
	VK_CHECK( qvkBindBufferMemory( vk.device, up.ring, up.ringMemory, 0 ) );
	VK_CHECK( qvkMapMemory( vk.device, up.ringMemory, 0, VK_WHOLE_SIZE, 0, (void **)&up.ringPtr ) );
	VK_SET_OBJECT_NAME( up.ring, "upload ring", VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT );

	VkCommandPoolCreateInfo cp;
	Com_Memset( &cp, 0, sizeof( cp ) );
	cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cp.queueFamilyIndex = vk.queue_family_index;
	VK_CHECK( qvkCreateCommandPool( vk.device, &cp, NULL, &up.pool ) );
	VkCommandBuffer cmds[UPLOAD_BATCHES];
	VkCommandBufferAllocateInfo ca;
	Com_Memset( &ca, 0, sizeof( ca ) );
	ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ca.commandPool = up.pool;
	ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ca.commandBufferCount = UPLOAD_BATCHES;
	VK_CHECK( qvkAllocateCommandBuffers( vk.device, &ca, cmds ) );
	VkFenceCreateInfo fi;
	Com_Memset( &fi, 0, sizeof( fi ) );
	fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	for ( int i = 0; i < UPLOAD_BATCHES; i++ ) {
		up.batches[i].cmd = cmds[i];
		VK_CHECK( qvkCreateFence( vk.device, &fi, NULL, &up.batches[i].fence ) );
		up.batches[i].id = 0;
		up.batches[i].submitted = false;
	}
	up.nextBatch = 1;
	up.recording = false;
	up.head = 0;
	up.segs.clear();
	up.ready = true;
	return true;
}

// a batch's copies have finished (or the batch was never submitted and is still recording: false)
static bool vk_upload_batch_done( uint64_t id ) {
	uploadBatch_t &b = up.batches[id % UPLOAD_BATCHES];
	if ( b.id != id ) {
		return true;	// its slot went to a later batch: it was waited for then
	}
	if ( !b.submitted ) {
		return false;
	}
	return qvkWaitForFences( vk.device, 1, &b.fence, VK_TRUE, 0 ) == VK_SUCCESS;
}

static void vk_upload_reclaim( void ) {
	size_t n = 0;
	while ( n < up.segs.size() && vk_upload_batch_done( up.segs[n].batch ) ) n++;
	if ( n ) {
		up.segs.erase( up.segs.begin(), up.segs.begin() + n );
	}
	if ( up.segs.empty() ) {
		up.head = 0;
	}
}

// the recording batch's command buffer; a slot still in flight is waited for (eight batches back)
static VkCommandBuffer vk_upload_cmd( void ) {
	uploadBatch_t &b = up.batches[up.nextBatch % UPLOAD_BATCHES];
	if ( !up.recording ) {
		if ( b.submitted ) {
			VK_CHECK( qvkWaitForFences( vk.device, 1, &b.fence, VK_TRUE, 1e10 ) );
		}
		VK_CHECK( qvkResetFences( vk.device, 1, &b.fence ) );
		b.id = up.nextBatch;
		b.submitted = false;
		VkCommandBufferBeginInfo bi;
		Com_Memset( &bi, 0, sizeof( bi ) );
		bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		VK_CHECK( qvkResetCommandBuffer( b.cmd, 0 ) );
		VK_CHECK( qvkBeginCommandBuffer( b.cmd, &bi ) );
		up.recording = true;
	}
	return b.cmd;
}

// ring space for n bytes, or false (full)
static bool vk_upload_ring_alloc( uint32_t n, uint32_t *offset ) {
	uint32_t start;
	if ( up.segs.empty() ) {
		start = 0;
	} else {
		const uint32_t tail = up.segs.front().start;
		if ( up.head >= tail ) {	// used: [tail, head)
			if ( up.head + n <= UPLOAD_RING ) {
				start = up.head;
			} else if ( n < tail ) {
				start = 0;
			} else {
				return false;
			}
		} else {					// used: [tail, end) and [0, head)
			if ( up.head + n < tail ) {
				start = up.head;
			} else {
				return false;
			}
		}
	}
	uploadSeg_t s;
	s.start = start;
	s.end = start + n;
	s.batch = up.nextBatch;
	up.segs.push_back( s );
	up.head = s.end;
	*offset = start;
	return true;
}

qboolean vk_upload_buffer( VkBuffer dst, VkDeviceSize dstOffset, const void *data, VkDeviceSize size ) {
	if ( !size ) {
		return qtrue;
	}
	if ( !vk_upload_init() ) {
		return qfalse;
	}
	vk_upload_reclaim();
	VkCommandBuffer cmd = vk_upload_cmd();
	const byte *src = (const byte *)data;
	VkDeviceSize done = 0;
	while ( done < size ) {
		const uint32_t n = (uint32_t)( size - done < UPLOAD_PIECE ? size - done : UPLOAD_PIECE );
		const uint32_t padded = ( n + UPLOAD_ALIGN - 1 ) & ~( UPLOAD_ALIGN - 1 );
		VkBufferCopy region;
		region.dstOffset = dstOffset + done;
		region.size = n;
		uint32_t offset;
		bool inRing = vk_upload_ring_alloc( padded, &offset );
		if ( !inRing ) {
			// full (a map load's static buffers, with no frame between them): wait for what is in flight rather
			// than hold more staging memory; a piece is at most a quarter of the ring
			vk_upload_finish();
			cmd = vk_upload_cmd();
			inRing = vk_upload_ring_alloc( padded, &offset );
		}
		if ( !inRing ) {
			return qfalse;	// not reached: the finish above emptied the ring
		}
		memcpy( up.ringPtr + offset, src + done, n );
		region.srcOffset = offset;
		qvkCmdCopyBuffer( cmd, up.ring, dst, 1, &region );
		done += n;
	}
	up.bytes += size;
	up.uploads++;
	return qtrue;
}

void vk_upload_flush( void ) {
	if ( !up.ready || !up.recording ) {
		return;
	}
	uploadBatch_t &b = up.batches[up.nextBatch % UPLOAD_BATCHES];
	// the copies before anything submitted later reads the buffers (vertex input, shaders, compute, transfers)
	VkMemoryBarrier mb;
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
	qvkCmdPipelineBarrier( b.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
	VK_CHECK( qvkEndCommandBuffer( b.cmd ) );
	VkSubmitInfo si;
	Com_Memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &b.cmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &si, b.fence ) );
	b.submitted = true;
	up.recording = false;
	up.nextBatch++;
}

// every upload recorded so far, finished (before the buffers they write are destroyed)
void vk_upload_finish( void ) {
	if ( !up.ready ) {
		return;
	}
	vk_upload_flush();
	for ( int i = 0; i < UPLOAD_BATCHES; i++ ) {
		if ( up.batches[i].submitted ) {
			VK_CHECK( qvkWaitForFences( vk.device, 1, &up.batches[i].fence, VK_TRUE, 1e10 ) );
		}
	}
	vk_upload_reclaim();
}

void vk_upload_shutdown( void ) {
	if ( !up.ready ) {
		return;
	}
	vk_upload_finish();
	up.segs.clear();
	for ( int i = 0; i < UPLOAD_BATCHES; i++ ) {
		qvkDestroyFence( vk.device, up.batches[i].fence, NULL );
	}
	qvkDestroyCommandPool( vk.device, up.pool, NULL );	// frees the batches' command buffers
	qvkUnmapMemory( vk.device, up.ringMemory );
	VK_DESTROY_BUFFER( vk.device, up.ring );
	VK_FREE_MEMORY( vk.device, up.ringMemory );
	Com_Memset( &up.batches, 0, sizeof( up.batches ) );
	up.pool = VK_NULL_HANDLE;
	up.ring = VK_NULL_HANDLE;
	up.ringMemory = VK_NULL_HANDLE;
	up.ringPtr = NULL;
	up.recording = false;
	up.ready = false;
}

void vk_upload_info( void ) {
	ri.Printf( PRINT_ALL, "uploads: %llu (%.1f MB) without a wait, ring %u MB, %d in-flight ranges\n",
		(unsigned long long)up.uploads, up.bytes / ( 1024.0 * 1024.0 ), UPLOAD_RING >> 20, (int)up.segs.size() );
}
