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
The renderer's device memory: one ledger and one budget.

Ledger. Every vkAllocateMemory and vkFreeMemory of the renderer passes through here (vk_vram_hook puts these
functions in place of the loaded entry points), so each allocation is counted by its kind: the name the allocation
macros give, or the tag the caller set (VK_VRAM_TAG), cut at its first ':'. The vkmem command lists the kinds and the
device-local heap's budget and use (VK_EXT_memory_budget); the use the ledger does not see is the driver's own (the
swapchain, the driver's internal allocations).

Budget. vk_vram_free() is what the device-local heap's budget leaves: the budget less the process's use, the use read
from the driver at most every 100 ms and carried between reads by the ledger's own changes. The texture stream pool
(vk_stream.cpp) fills what the budget leaves less r_vramReserveMB and gives images back when the rest grows; images
that do not stream are admitted against vk_vram_free() (vk_image.cpp), dropping their top levels or, as a last resort,
going to system memory instead of failing the session.
*/
#include "tr_local.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

static PFN_vkAllocateMemory	vram_realAlloc;
static PFN_vkFreeMemory		vram_realFree;

struct vramKind_t {
	std::string		name;
	VkDeviceSize	device, host;		// bytes live
	VkDeviceSize	peakDevice;
	int				count;
};

struct vramEntry_t {
	VkDeviceSize	size;
	int				kind;
	bool			device;
};

static std::mutex								vram_lock;
static std::vector<vramKind_t>					vram_kinds;
static std::unordered_map<std::string, int>		vram_kindIndex;
static std::unordered_map<VkDeviceMemory, vramEntry_t>	vram_live;
static VkDeviceSize								vram_device, vram_host;	// live totals
static thread_local const char					*vram_tag;
static uint32_t									vram_typeDevice;			// bit per memory type: device-local heap

// the budget
static int										vram_heap = -2;			// -2 not looked for, -1 none
static VkDeviceSize								vram_heapSize;
static PFN_vkGetPhysicalDeviceMemoryProperties2	vram_getProps2;
static VkDeviceSize								vram_budget, vram_usage, vram_ledgerAtRead;
static int										vram_readMs;
static cvar_t									*r_vramReserveMB, *r_vramFloorMB, *r_vramBudgetMB;
static int										vram_degraded, vram_spilled;	// images loaded smaller, chunks in system memory (this map)
static int										vram_spilledBuffers;			// buffers in system memory (this map)

static int vram_kind_locked( const char *name ) {
	char buf[64];
	if ( !name || !name[0] ) {
		name = "untagged";
	}
	int i = 0;
	for ( ; name[i] && name[i] != ':' && i < (int)sizeof( buf ) - 1; i++ ) {
		buf[i] = name[i];
	}
	buf[i] = 0;
	const std::string key( buf );
	auto it = vram_kindIndex.find( key );
	if ( it != vram_kindIndex.end() ) {
		return it->second;
	}
	vramKind_t k;
	k.name = key;
	k.device = k.host = k.peakDevice = 0;
	k.count = 0;
	vram_kinds.push_back( k );
	vram_kindIndex[key] = (int)vram_kinds.size() - 1;
	return (int)vram_kinds.size() - 1;
}

static VKAPI_ATTR VkResult VKAPI_CALL vram_allocate( VkDevice device, const VkMemoryAllocateInfo *info, const VkAllocationCallbacks *cb, VkDeviceMemory *out ) {
	const VkResult r = vram_realAlloc( device, info, cb, out );
	if ( r == VK_SUCCESS && *out ) {
		std::lock_guard<std::mutex> g( vram_lock );
		vramEntry_t e;
		e.size = info->allocationSize;
		e.kind = vram_kind_locked( vram_tag );
		e.device = info->memoryTypeIndex < 32 && ( vram_typeDevice & ( 1u << info->memoryTypeIndex ) );
		vram_live[*out] = e;
		vramKind_t &k = vram_kinds[e.kind];
		k.count++;
		if ( e.device ) {
			k.device += e.size;
			vram_device += e.size;
			k.peakDevice = MAX( k.peakDevice, k.device );
		} else {
			k.host += e.size;
			vram_host += e.size;
		}
	}
	return r;
}

static VKAPI_ATTR void VKAPI_CALL vram_free( VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks *cb ) {
	if ( memory ) {
		std::lock_guard<std::mutex> g( vram_lock );
		auto it = vram_live.find( memory );
		if ( it != vram_live.end() ) {
			vramKind_t &k = vram_kinds[it->second.kind];
			k.count--;
			if ( it->second.device ) {
				k.device -= MIN( k.device, it->second.size );
				vram_device -= MIN( vram_device, it->second.size );
			} else {
				k.host -= MIN( k.host, it->second.size );
				vram_host -= MIN( vram_host, it->second.size );
			}
			vram_live.erase( it );
		}
	}
	vram_realFree( device, memory, cb );
}

const char *vk_vram_set_tag( const char *kind ) {
	const char *prev = vram_tag;
	vram_tag = kind;
	return prev;
}

VkResult vk_vram_allocate_named( VkDevice device, const VkMemoryAllocateInfo *info, VkDeviceMemory *out, const char *kind ) {
	const char *prev = vk_vram_set_tag( kind );
	const VkResult r = qvkAllocateMemory( device, info, NULL, out );
	vk_vram_set_tag( prev );
	return r;
}

static void vram_find_heap( void ) {
	if ( vram_heap != -2 ) {
		return;
	}
	VkPhysicalDeviceMemoryProperties mp;
	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &mp );
	vram_heap = -1;
	vram_heapSize = 0;
	for ( uint32_t i = 0; i < mp.memoryHeapCount; i++ ) {
		if ( ( mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ) && mp.memoryHeaps[i].size > vram_heapSize ) {
			vram_heapSize = mp.memoryHeaps[i].size;
			vram_heap = (int)i;
		}
	}
	vram_typeDevice = 0;
	for ( uint32_t i = 0; i < mp.memoryTypeCount && i < 32; i++ ) {
		if ( (int)mp.memoryTypes[i].heapIndex == vram_heap ) {
			vram_typeDevice |= 1u << i;
		}
	}
	bool ext = false;	// (asked of the device: vk.device_extensions_string is cut at MAX_STRING_CHARS)
	uint32_t count = 0;
	if ( vk.instanceApiVersion >= VK_API_VERSION_1_1 && qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &count, NULL ) == VK_SUCCESS && count ) {
		std::vector<VkExtensionProperties> e( count );
		if ( qvkEnumerateDeviceExtensionProperties( vk.physical_device, NULL, &count, e.data() ) == VK_SUCCESS ) {
			for ( uint32_t i = 0; i < count && !ext; i++ ) {
				ext = strcmp( e[i].extensionName, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME ) == 0;
			}
		}
	}
	vram_getProps2 = ext ? (PFN_vkGetPhysicalDeviceMemoryProperties2)qvkGetInstanceProcAddr( vk.instance, "vkGetPhysicalDeviceMemoryProperties2" ) : NULL;
	vram_readMs = -100000;
}

// after the device's functions are loaded
void vk_vram_hook( void ) {
	if ( qvkAllocateMemory != vram_allocate ) {
		vram_realAlloc = qvkAllocateMemory;
		vram_realFree = qvkFreeMemory;
		qvkAllocateMemory = vram_allocate;
		qvkFreeMemory = vram_free;
	}
	vram_heap = -2;
	vram_find_heap();
	r_vramReserveMB = ri.Cvar_Get( "r_vramReserveMB", "1024", CVAR_ARCHIVE_ND, "VRAM the texture stream pool leaves free for loads (other textures, models, the scene's buffers), in MB" );
	r_vramFloorMB = ri.Cvar_Get( "r_vramFloorMB", "256", CVAR_ARCHIVE_ND, "VRAM kept free under the budget: a texture that does not stream and does not fit above it loads with its top levels dropped, in MB" );
	r_vramBudgetMB = ri.Cvar_Get( "r_vramBudgetMB", "0", CVAR_ARCHIVE_ND, "The VRAM budget is at most this many MB (0: the driver's own figure): this PC run as a smaller card, e.g. 5600 for a 6 GB one" );
}

// the driver's budget and the process's use of the device-local heap, read at most every 100 ms; between reads the use
// moves with the ledger's own allocations. false without VK_EXT_memory_budget (then budget is the heap's size, use
// the ledger's device-local total)
static bool vram_read( VkDeviceSize *budget, VkDeviceSize *usage ) {
	vram_find_heap();
	VkDeviceSize ledger;
	{
		std::lock_guard<std::mutex> g( vram_lock );
		ledger = vram_device;
	}
	const VkDeviceSize cap = r_vramBudgetMB && r_vramBudgetMB->integer > 0 ? (VkDeviceSize)r_vramBudgetMB->integer << 20 : ~(VkDeviceSize)0;
	if ( !vram_getProps2 || vram_heap < 0 ) {
		*budget = MIN( vram_heapSize, cap );
		*usage = ledger;
		return false;
	}
	const int now = ri.Milliseconds();
	if ( now - vram_readMs >= 100 || now < vram_readMs ) {
		VkPhysicalDeviceMemoryBudgetPropertiesEXT b;
		VkPhysicalDeviceMemoryProperties2 p2;
		Com_Memset( &b, 0, sizeof( b ) );
		Com_Memset( &p2, 0, sizeof( p2 ) );
		b.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT;
		p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
		p2.pNext = &b;
		vram_getProps2( vk.physical_device, &p2 );
		vram_budget = b.heapBudget[vram_heap];
		vram_usage = b.heapUsage[vram_heap];
		vram_ledgerAtRead = ledger;
		vram_readMs = now;
	}
	*budget = MIN( vram_budget, cap );
	const VkDeviceSize u = vram_usage + ledger;
	*usage = u > vram_ledgerAtRead ? u - vram_ledgerAtRead : 0;
	return true;
}

VkDeviceSize vk_vram_budget( VkDeviceSize *usage ) {
	VkDeviceSize b, u;
	vram_read( &b, &u );
	if ( usage ) {
		*usage = u;
	}
	return b;
}

VkDeviceSize vk_vram_free( void ) {
	VkDeviceSize b, u;
	vram_read( &b, &u );
	return b > u ? b - u : 0;
}

VkDeviceSize vk_vram_reserve( void ) {
	return (VkDeviceSize)MAX( r_vramReserveMB ? r_vramReserveMB->integer : 1024, 0 ) << 20;
}

// what a texture that does not stream may take: what the budget leaves less r_vramFloorMB
VkDeviceSize vk_vram_admit( void ) {
	const VkDeviceSize f = vk_vram_free(), floor = (VkDeviceSize)MAX( r_vramFloorMB ? r_vramFloorMB->integer : 256, 0 ) << 20;
	return f > floor ? f - floor : 0;
}

// the top levels a texture that does not stream drops to fit what the budget admits: from skip, one more while its top
// level is over 256 texels across, a level below it is kept (maxSkip) and bytes( skip ) is more than admitted
int vk_vram_fit( int width, int height, int skip, int maxSkip, VkDeviceSize ( *bytes )( int skip, const void *ctx ), const void *ctx ) {
	const VkDeviceSize avail = vk_vram_admit();
	while ( skip < maxSkip && ( MAX( width, height ) >> skip ) > 256 && bytes( skip, ctx ) > avail ) {
		skip++;
	}
	return skip;
}

// an image loaded smaller than its file for want of VRAM (the first few named)
void vk_vram_degraded( const char *name, int fromW, int fromH, int toW, int toH ) {
	if ( vram_degraded++ < 16 ) {
		ri.Printf( PRINT_WARNING, "VRAM: %s loaded at %ix%i, not %ix%i (the budget is spent; vkmem)\n", name, toW, toH, fromW, fromH );
	}
}

// an image chunk in system memory: the device had no room at all
void vk_vram_spilled( VkDeviceSize bytes ) {
	if ( vram_spilled++ < 4 ) {
		ri.Printf( PRINT_WARNING, "VRAM: no room on the device; %.1f MB of images in system memory (slower; vkmem)\n", bytes / ( 1024.0 * 1024.0 ) );
	}
}

// a device-local resource's memory type: VRAM while the budget has room for it, else system memory (slower, but the
// device is not oversubscribed: past its budget the driver pages, and a frame that touches everything can time out)
uint32_t vk_vram_memory_type( uint32_t typeBits, VkDeviceSize size ) {
	if ( vk_vram_admit() >= size ) {
		return vk_find_memory_type( typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	}
	const uint32_t t = vk_vram_system_type( typeBits );
	if ( t == 0xFFFFFFFFu ) {
		return vk_find_memory_type( typeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	}
	if ( vram_spilledBuffers++ < 4 ) {
		ri.Printf( PRINT_WARNING, "VRAM: the budget is spent; a %.1f MB buffer in system memory (slower; vkmem)\n", size / ( 1024.0 * 1024.0 ) );
	}
	return t;
}

// a map load: the warnings above name the first few again, and vkmem counts this map's
void vk_vram_map_start( void ) {
	vram_degraded = vram_spilled = vram_spilledBuffers = 0;
}

// a memory type in system memory for a resource that has no room in VRAM (the last resort): host-visible, not on the
// device-local heap; 0xFFFFFFFF: none the resource can use
uint32_t vk_vram_system_type( uint32_t typeBits ) {
	vram_find_heap();
	VkPhysicalDeviceMemoryProperties mp;
	qvkGetPhysicalDeviceMemoryProperties( vk.physical_device, &mp );
	for ( uint32_t i = 0; i < mp.memoryTypeCount; i++ ) {
		if ( ( typeBits & ( 1u << i ) ) && !( vram_typeDevice & ( 1u << i ) ) ) {
			return i;
		}
	}
	return 0xFFFFFFFFu;
}

void vk_vram_info_f( void ) {
	std::vector<vramKind_t> k;
	VkDeviceSize device, host;
	{
		std::lock_guard<std::mutex> g( vram_lock );
		k = vram_kinds;
		device = vram_device;
		host = vram_host;
	}
	std::sort( k.begin(), k.end(), []( const vramKind_t &a, const vramKind_t &b ) { return a.device + a.host > b.device + b.host; } );
	const double M = 1024.0 * 1024.0;
	ri.Printf( PRINT_ALL, "vkmem: %-34s %8s %8s %8s %6s\n", "kind", "VRAM MB", "peak MB", "sys MB", "count" );
	for ( size_t i = 0; i < k.size(); i++ ) {
		if ( k[i].count <= 0 && !k[i].peakDevice ) {
			continue;
		}
		ri.Printf( PRINT_ALL, "vkmem: %-34s %8.1f %8.1f %8.1f %6d\n", k[i].name.c_str(), k[i].device / M, k[i].peakDevice / M, k[i].host / M, k[i].count );
	}
	VkDeviceSize b, u;
	const bool ext = vram_read( &b, &u );
	vram_readMs = -100000;	// a fresh read for the totals
	vram_read( &b, &u );
	ri.Printf( PRINT_ALL, "vkmem: ledger %.1f MB in VRAM, %.1f MB in system memory; since the map load %d images loaded smaller for want of VRAM, %d image chunks and %d buffers in system memory\n",
		device / M, host / M, vram_degraded, vram_spilled, vram_spilledBuffers );
	if ( ext ) {
		ri.Printf( PRINT_ALL, "vkmem: device-local heap %.0f MB: budget %.1f MB, process use %.1f MB (%.1f MB not in the ledger: the driver's, the swapchain), free %.1f MB, reserve %.0f MB\n",
			vram_heapSize / M, b / M, u / M, u > device ? ( u - device ) / M : 0.0, b > u ? ( b - u ) / M : 0.0, vk_vram_reserve() / M );
	} else {
		ri.Printf( PRINT_ALL, "vkmem: device-local heap %.0f MB (no VK_EXT_memory_budget)\n", vram_heapSize / M );
	}
}
