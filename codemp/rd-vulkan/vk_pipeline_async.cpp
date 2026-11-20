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
Pipelines compiled off the main thread (docs/character-loading-plan.md, mesh steps).

A pipeline is created the first time a draw binds it in a render pass (vk_gen_pipeline); one the driver has never
compiled costs 100-200 ms there, 0.5 ms from the saved pipeline cache. A player model warm-up asks for its pipelines
ahead of time instead: inside vk_pipeline_defer( qtrue ) a shader's creation compiles nothing (vk_find_pipeline_ext;
a Ghoul2 VBO draw never binds those defs as they are), and the warm-up's pipeline step requests the variants a Ghoul2
draw derives from them (vk_request_ghoul2_pipelines, vk_model_warm.cpp). A worker
thread compiles requests in order with the shared pipeline cache (vkCreateGraphicsPipelines and the cache are safe
from another thread; vk_build_pipeline reads only settled state and cvars, and its vertex input arrays are per thread;
its vk_debug prints only in _DEBUG builds); the main thread adopts the
results once a frame (vk_pipeline_adopt) into vk.pipelines[].handle[]. A draw that needs one still queued takes it
back and compiles it itself, one being compiled it waits for: never twice, never a frame later than before.
vk_destroy_pipelines (a swapchain or renderer restart) stops the worker first.
*/
#include "tr_local.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

struct pipeJob_t {
	uint32_t		index;
	renderPass_t	pass;
	Vk_Pipeline_Def	def;		// a copy: vk.pipelines may grow meanwhile (it never moves)
	vkPipelineSettings_t	settings;	// the cvars it reads, captured on the main thread when queued
	int				state;		// PJ_*
	VkPipeline		result;
	double			ms;
};

enum { PJ_QUEUED, PJ_COMPILING, PJ_DONE };

static struct {
	std::vector<pipeJob_t *>	jobs;		// in request order
	std::mutex					lock;
	std::condition_variable		wake;		// the worker
	std::condition_variable		done;		// a waiting draw
	std::thread					*worker;	// by pointer: an exit that skips the shutdown (Sys_Error) must not destroy a joinable std::thread (std::terminate)
	bool						running, quit, defer;
	int							compiled, adopted, taken;
	double						workerMs;
} pa;

static void vk_pipeline_worker( void ) {
	std::unique_lock<std::mutex> lk( pa.lock );
	for ( ;; ) {
		pipeJob_t *j = NULL;
		pa.wake.wait( lk, [&j]() {
			if ( pa.quit ) return true;
			for ( size_t i = 0; i < pa.jobs.size() && !j; i++ ) {
				if ( pa.jobs[i]->state == PJ_QUEUED ) j = pa.jobs[i];
			}
			return j != NULL;
		} );
		if ( pa.quit ) {
			return;
		}
		j->state = PJ_COMPILING;
		lk.unlock();
		const auto t0 = std::chrono::steady_clock::now();
		const VkPipeline p = vk_build_pipeline_ex( &j->def, j->pass, &j->settings );
		const double ms = std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - t0 ).count();
		lk.lock();
		j->result = p;
		j->ms = ms;
		j->state = PJ_DONE;
		pa.compiled++;
		pa.workerMs += ms;
		pa.done.notify_all();
	}
}

void vk_pipeline_defer( qboolean defer ) {
	pa.defer = defer ? true : false;
}

qboolean vk_pipeline_deferring( void ) {
	return pa.defer ? qtrue : qfalse;
}

static pipeJob_t *vk_pipeline_job( uint32_t index, renderPass_t pass ) {
	for ( size_t i = 0; i < pa.jobs.size(); i++ ) {
		if ( pa.jobs[i]->index == index && pa.jobs[i]->pass == pass ) return pa.jobs[i];
	}
	return NULL;
}

void vk_pipeline_request( uint32_t index, renderPass_t pass ) {
	if ( index >= vk.pipelines_count || pass < 0 || pass >= RENDER_PASS_COUNT || vk.pipelines[index].handle[pass] ) {
		return;
	}
	{
		std::lock_guard<std::mutex> lk( pa.lock );
		if ( vk_pipeline_job( index, pass ) ) {
			return;
		}
		pipeJob_t *j = new pipeJob_t();
		j->index = index;
		j->pass = pass;
		j->def = vk.pipelines[index].def;
		j->settings = vk_pipeline_settings();
		j->state = PJ_QUEUED;
		j->result = VK_NULL_HANDLE;
		j->ms = 0.0;
		pa.jobs.push_back( j );
	}
	if ( !pa.running ) {
		pa.quit = false;
		pa.worker = new std::thread( vk_pipeline_worker );
		pa.running = true;
	}
	pa.wake.notify_one();
}

qboolean vk_pipeline_inflight( uint32_t index, renderPass_t pass ) {
	std::lock_guard<std::mutex> lk( pa.lock );
	return vk_pipeline_job( index, pass ) ? qtrue : qfalse;
}

// a finished job's pipeline into its slot (main thread, job already out of the list)
static void vk_pipeline_install( pipeJob_t *j ) {
	VkPipeline *slot = &vk.pipelines[j->index].handle[j->pass];
	if ( j->result != VK_NULL_HANDLE ) {
		if ( *slot == VK_NULL_HANDLE ) {
			*slot = j->result;
			VK_SET_OBJECT_NAME( j->result, va( "pipeline def#%i, pass#%i", j->index, j->pass ), VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT );
			vk.pipeline_create_count++;
			pa.adopted++;
			if ( j->ms > 20.0 ) {
				ri.Printf( PRINT_DEVELOPER, "pipeline def#%u pass %d: compiled on the worker in %.0f ms\n", j->index, (int)j->pass, j->ms );
			}
		} else {
			qvkDestroyPipeline( vk.device, j->result, NULL );
		}
	}
	delete j;
}

void vk_pipeline_adopt( void ) {
	std::vector<pipeJob_t *> finished;
	{
		std::lock_guard<std::mutex> lk( pa.lock );
		for ( size_t i = 0; i < pa.jobs.size(); ) {
			if ( pa.jobs[i]->state == PJ_DONE ) {
				finished.push_back( pa.jobs[i] );
				pa.jobs.erase( pa.jobs.begin() + i );
			} else {
				i++;
			}
		}
	}
	for ( size_t i = 0; i < finished.size(); i++ ) {
		vk_pipeline_install( finished[i] );
	}
}

// vk_gen_pipeline, for a slot with no pipeline: a queued request is withdrawn (the caller compiles it now), one being
// compiled is waited for and installed. True when the slot has its pipeline afterwards.
qboolean vk_pipeline_take( uint32_t index, renderPass_t pass ) {
	pipeJob_t *j;
	{
		std::unique_lock<std::mutex> lk( pa.lock );
		j = vk_pipeline_job( index, pass );
		if ( !j ) {
			return qfalse;
		}
		if ( j->state == PJ_QUEUED ) {
			for ( size_t i = 0; i < pa.jobs.size(); i++ ) {
				if ( pa.jobs[i] == j ) {
					pa.jobs.erase( pa.jobs.begin() + i );
					break;
				}
			}
			delete j;
			pa.taken++;
			return qfalse;
		}
		// a compile takes 0.1-1 s; past 2 s the wait names its job (the main thread was once stuck 13 s after a
		// picker Apply, cause not found)
		for ( int waited = 0; !pa.done.wait_for( lk, std::chrono::seconds( 2 ), [j]() { return j->state == PJ_DONE; } ); ) {
			waited += 2;
			ri.Printf( PRINT_WARNING, "pipeline def#%u pass %d: still compiling on the worker after %d s\n", j->index, (int)j->pass, waited );
		}
		for ( size_t i = 0; i < pa.jobs.size(); i++ ) {
			if ( pa.jobs[i] == j ) {
				pa.jobs.erase( pa.jobs.begin() + i );
				break;
			}
		}
	}
	vk_pipeline_install( j );
	return vk.pipelines[index].handle[pass] != VK_NULL_HANDLE ? qtrue : qfalse;
}

// before pipelines, render passes or the cache go: the worker stopped, its results installed or destroyed
void vk_pipeline_async_shutdown( void ) {
	if ( pa.running ) {
		{
			std::lock_guard<std::mutex> lk( pa.lock );
			pa.quit = true;
		}
		pa.wake.notify_all();
		pa.worker->join();	// it finishes the pipeline it is on
		delete pa.worker;
		pa.worker = NULL;
		pa.running = false;
	}
	for ( size_t i = 0; i < pa.jobs.size(); i++ ) {
		pipeJob_t *j = pa.jobs[i];
		if ( j->state == PJ_DONE && j->result != VK_NULL_HANDLE ) {
			qvkDestroyPipeline( vk.device, j->result, NULL );	// its slot may be about to go: compile again on demand
		}
		delete j;
	}
	pa.jobs.clear();
	pa.quit = false;
	pa.defer = false;
}

void vk_pipeline_async_info( void ) {
	std::lock_guard<std::mutex> lk( pa.lock );
	ri.Printf( PRINT_ALL, "pipeline worker: %d compiled (%.0f ms), %d installed, %d taken back by a draw, %d pending\n",
		pa.compiled, pa.workerMs, pa.adopted, pa.taken, (int)pa.jobs.size() );
}
