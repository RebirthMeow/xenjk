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

// cl_renderer_select.cpp -- which renderer the client starts, and the fall back from the Vulkan renderer.
//
// - The branded client (the binary is named xenjk.*, a copy of openjk.*) starts the renderer named by
//   xenjk_renderer (archived, default rd-vulkan) instead of cl_renderer from the config files, so an old config's
//   "seta cl_renderer rd-vanilla" does not decide it; +set cl_renderer on the command line still wins, and a
//   cl_renderer change followed by vid_restart is kept in xenjk_renderer. openjk.* keeps stock behaviour.
// - Before rd-vulkan is loaded, CL_VK_Probe checks that a Vulkan loader, an instance with window-surface support
//   and a device with a graphics queue and swapchain support exist; when not, the client starts rd-vanilla.
// - The branded client marks the renderer start in its home directory and clears the mark once the renderer is
//   up: a start that ended the process (a fatal error inside Vulkan initialisation) is found at the next launch,
//   which starts rd-vanilla and sets xenjk_renderer to it.

#include "client.h"
#include "sys/sys_loadlib.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#endif

#define VK_NO_PROTOTYPES
#include "rd-vulkan/vulkan/vulkan_core.h"

#define CL_FALLBACK_RENDERER	"rd-vanilla"
#define CL_VULKAN_RENDERER		"rd-vulkan"
#define CL_RENDERER_MARK		"xenjk-renderer-start.txt"

extern cvar_t *cl_renderer;	// cl_main.cpp

static cvar_t *xenjk_renderer;
static qboolean clVulkanFellBack;	// this process's probe found Vulkan unusable and started rd-vanilla instead

static unsigned long CL_ProcessId( void ) {
#ifdef _WIN32
	return (unsigned long)GetCurrentProcessId();
#else
	return (unsigned long)getpid();
#endif
}

// another process with this id is running (the mark is then another client's start in progress)
static qboolean CL_OtherProcessAlive( unsigned long pid ) {
	if ( !pid || pid == CL_ProcessId() )
		return qfalse;
#ifdef _WIN32
	HANDLE h = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid );
	if ( !h )
		return ( GetLastError() == ERROR_ACCESS_DENIED ) ? qtrue : qfalse;
	DWORD code = 0;
	const BOOL ok = GetExitCodeProcess( h, &code );
	CloseHandle( h );
	return ( ok && code == STILL_ACTIVE ) ? qtrue : qfalse;
#else
	return ( kill( (pid_t)pid, 0 ) == 0 || errno == EPERM ) ? qtrue : qfalse;
#endif
}

/*
============
CL_IsBrandedClient

True when the running binary is named xenjk.* (any case).
============
*/
qboolean CL_IsBrandedClient( void ) {
	return Sys_IsBrandedBinary();
}

/*
============
CL_VK_Probe

Load the Vulkan loader on its own (no window and no SDL video subsystem needed), create an instance with the window
surface extensions and look for a device with a graphics queue and VK_KHR_swapchain. why receives the reason on
failure.
============
*/
qboolean CL_VK_Probe( char *why, int whySize ) {
#if defined(_WIN32)
	static const char *loaderNames[] = { "vulkan-1.dll" };
	static const char *surfaceExtensions[] = { "VK_KHR_surface", "VK_KHR_win32_surface" };
#elif defined(MACOS_X)
	static const char *loaderNames[] = { "libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib" };
	static const char *surfaceExtensions[] = { "VK_KHR_surface" };
#else
	static const char *loaderNames[] = { "libvulkan.so.1", "libvulkan.so" };
	static const char *surfaceExtensions[] = { "VK_KHR_surface" };
#endif
	void *lib = NULL;
	PFN_vkGetInstanceProcAddr getInstanceProcAddr;
	PFN_vkCreateInstance createInstance;
	PFN_vkDestroyInstance destroyInstance;
	PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices;
	PFN_vkGetPhysicalDeviceQueueFamilyProperties getQueueFamilies;
	PFN_vkEnumerateDeviceExtensionProperties enumerateDeviceExtensions;
	VkInstance instance = VK_NULL_HANDLE;
	VkApplicationInfo app;
	VkInstanceCreateInfo info;
	VkPhysicalDevice devices[16];
	uint32_t numDevices = ARRAY_LEN( devices );
	VkResult res;
	qboolean found = qfalse;
	size_t i;

	why[0] = '\0';

	for ( i = 0; i < ARRAY_LEN( loaderNames ) && !lib; i++ )
		lib = Sys_LoadLibrary( loaderNames[i] );
	if ( !lib ) {
		Q_strncpyz( why, "no Vulkan loader (" , whySize );
		Q_strcat( why, whySize, loaderNames[0] );
		Q_strcat( why, whySize, ")" );
		return qfalse;
	}

	getInstanceProcAddr = (PFN_vkGetInstanceProcAddr)Sys_LoadFunction( lib, "vkGetInstanceProcAddr" );
	createInstance = getInstanceProcAddr ? (PFN_vkCreateInstance)getInstanceProcAddr( VK_NULL_HANDLE, "vkCreateInstance" ) : NULL;
	if ( !createInstance ) {
		Q_strncpyz( why, "the Vulkan loader has no vkCreateInstance", whySize );
		Sys_UnloadLibrary( lib );
		return qfalse;
	}

	Com_Memset( &app, 0, sizeof( app ) );
	app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app.pApplicationName = "xenjk renderer probe";
	app.apiVersion = VK_API_VERSION_1_0;

	Com_Memset( &info, 0, sizeof( info ) );
	info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	info.pApplicationInfo = &app;
	info.enabledExtensionCount = ARRAY_LEN( surfaceExtensions );
	info.ppEnabledExtensionNames = surfaceExtensions;

	res = createInstance( &info, NULL, &instance );
	if ( res != VK_SUCCESS ) {
		Com_sprintf( why, whySize, "vkCreateInstance with window-surface extensions failed (%d)", (int)res );
		Sys_UnloadLibrary( lib );
		return qfalse;
	}

	destroyInstance = (PFN_vkDestroyInstance)getInstanceProcAddr( instance, "vkDestroyInstance" );
	enumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)getInstanceProcAddr( instance, "vkEnumeratePhysicalDevices" );
	getQueueFamilies = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)getInstanceProcAddr( instance, "vkGetPhysicalDeviceQueueFamilyProperties" );
	enumerateDeviceExtensions = (PFN_vkEnumerateDeviceExtensionProperties)getInstanceProcAddr( instance, "vkEnumerateDeviceExtensionProperties" );

	if ( !destroyInstance || !enumeratePhysicalDevices || !getQueueFamilies || !enumerateDeviceExtensions ) {
		Q_strncpyz( why, "the Vulkan instance lacks core entry points", whySize );
	} else {
		res = enumeratePhysicalDevices( instance, &numDevices, devices );
		if ( ( res != VK_SUCCESS && res != VK_INCOMPLETE ) || numDevices == 0 ) {
			Com_sprintf( why, whySize, "no Vulkan devices (%d)", (int)res );
		} else {
			for ( uint32_t d = 0; d < numDevices && !found; d++ ) {
				VkQueueFamilyProperties families[32];
				uint32_t numFamilies = ARRAY_LEN( families );
				qboolean graphics = qfalse, swapchain = qfalse;

				getQueueFamilies( devices[d], &numFamilies, families );
				for ( uint32_t f = 0; f < numFamilies; f++ ) {
					if ( families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT )
						graphics = qtrue;
				}

				uint32_t numExtensions = 0;
				if ( enumerateDeviceExtensions( devices[d], NULL, &numExtensions, NULL ) == VK_SUCCESS && numExtensions ) {
					VkExtensionProperties *extensions = (VkExtensionProperties *)malloc( numExtensions * sizeof( *extensions ) );
					if ( enumerateDeviceExtensions( devices[d], NULL, &numExtensions, extensions ) == VK_SUCCESS ) {
						for ( uint32_t e = 0; e < numExtensions; e++ ) {
							if ( !strcmp( extensions[e].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME ) )
								swapchain = qtrue;
						}
					}
					free( extensions );
				}

				if ( graphics && swapchain )
					found = qtrue;
			}
			if ( !found )
				Q_strncpyz( why, "no Vulkan device with a graphics queue and swapchain support", whySize );
		}
		destroyInstance( instance, NULL );
	}

	Sys_UnloadLibrary( lib );
	return found;
}

static const char *CL_RendererMarkPath( void ) {
	const char *home = Cvar_VariableString( "fs_homepath" );
	if ( !home[0] )
		return NULL;
	return va( "%s%c%s", home, PATH_SEP, CL_RENDERER_MARK );
}

/*
============
CL_SelectRenderer

Called by CL_InitRef after cl_renderer is registered and before the renderer library is loaded; may change
cl_renderer.
============
*/
void CL_SelectRenderer( void ) {
	static qboolean started = qfalse;
	const qboolean firstStart = started ? qfalse : qtrue;

	started = qtrue;

	if ( CL_IsBrandedClient() ) {
		xenjk_renderer = Cvar_Get( "xenjk_renderer", CL_VULKAN_RENDERER, CVAR_ARCHIVE,
			"Renderer the xenjk.* client starts (+set cl_renderer on the command line overrides it; rd-vanilla is the fall back)" );

		if ( firstStart ) {
			if ( Com_CommandLineSets( "cl_renderer" ) ) {
				Com_Printf( "xenjk: cl_renderer %s from the command line\n", cl_renderer->string );
			} else if ( Q_stricmp( cl_renderer->string, xenjk_renderer->string ) ) {
				Com_Printf( "xenjk: starting %s (xenjk_renderer; the configs' cl_renderer was %s)\n",
					xenjk_renderer->string, cl_renderer->string );
				Cvar_Set2( "cl_renderer", xenjk_renderer->string, 0, qtrue );
			}

			// a start that never finished: the process ended inside the renderer's initialisation
			const char *mark = CL_RendererMarkPath();
			FILE *f = mark ? fopen( mark, "rb" ) : NULL;
			if ( f ) {
				char failed[MAX_QPATH] = { 0 };
				char pidLine[32] = { 0 };
				if ( !fgets( failed, sizeof( failed ), f ) )
					failed[0] = '\0';
				if ( !fgets( pidLine, sizeof( pidLine ), f ) )
					pidLine[0] = '\0';
				fclose( f );
				for ( char *c = failed; *c; c++ ) {
					if ( *c == '\r' || *c == '\n' ) {
						*c = '\0';
						break;
					}
				}

				// a second client started while the first is still inside its renderer start: not a failure,
				// and the mark stays for that client to clear
				if ( CL_OtherProcessAlive( strtoul( pidLine, NULL, 10 ) ) ) {
					failed[0] = '\0';
				} else {
					remove( mark );
				}

				if ( failed[0] && !Q_stricmp( failed, cl_renderer->string ) && Q_stricmp( failed, CL_FALLBACK_RENDERER ) ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: the last start of %s did not finish; starting " CL_FALLBACK_RENDERER
						" and setting xenjk_renderer to it (set xenjk_renderer %s to try again)\n", failed, failed );
					Cvar_Set2( "cl_renderer", CL_FALLBACK_RENDERER, 0, qtrue );
					Cvar_Set( "xenjk_renderer", CL_FALLBACK_RENDERER );
				}
			}
		} else {
			// vid_restart: keep what the player chose for the next launch (not the probe's fall back)
			if ( !clVulkanFellBack || Q_stricmp( cl_renderer->string, CL_FALLBACK_RENDERER ) )
				Cvar_Set( "xenjk_renderer", cl_renderer->string );
		}
	}

	if ( !Q_stricmp( cl_renderer->string, CL_VULKAN_RENDERER ) ) {
		static int	probed = -1;	// once a process: -1 not yet, 0 failed, 1 ok
		static char	why[256];

		if ( probed < 0 )
			probed = CL_VK_Probe( why, sizeof( why ) ) ? 1 : 0;
		if ( probed ) {
			Com_Printf( "Vulkan probe: ok\n" );
		} else {
			Com_Printf( S_COLOR_YELLOW "WARNING: Vulkan is not usable here (%s); starting " CL_FALLBACK_RENDERER " instead\n", why );
			Cvar_Set2( "cl_renderer", CL_FALLBACK_RENDERER, 0, qtrue );
			clVulkanFellBack = qtrue;
		}
	}
}

/*
============
CL_RendererStarting / CL_RendererStarted

Around the renderer's initialisation (BeginRegistration) in the branded client.
============
*/
void CL_RendererStarting( void ) {
	if ( !CL_IsBrandedClient() || !Q_stricmp( cl_renderer->string, CL_FALLBACK_RENDERER ) )
		return;

	const char *mark = CL_RendererMarkPath();
	FILE *f = mark ? fopen( mark, "wb" ) : NULL;
	if ( f ) {
		fprintf( f, "%s\n%lu\n", cl_renderer->string, CL_ProcessId() );
		fclose( f );
	}
}

void CL_RendererStarted( void ) {
	if ( !CL_IsBrandedClient() )
		return;

	const char *mark = CL_RendererMarkPath();
	if ( mark )
		remove( mark );
}
