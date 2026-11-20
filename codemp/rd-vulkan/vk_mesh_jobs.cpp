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
GLM builds off the main thread (docs/character-loading-plan.md, mesh steps).

A player model warm-up registers its GLM inside R_MeshJobsDefer( qtrue ), and R_BuildMDXM then queues a job instead
of building every LOD in that frame. So does any other player GLM registered while a map runs (from the first world
scene after a registration or world load: R_MeshJobsLevelLoad, which covers listen servers, remote servers, demos and
vid_restart alike; a map load builds at once as before): on a listen server the game module registers another player's new model in the server frame
before cgame hears of it (docs/character-loading-plan.md, M2b); cgame's warm-up of that model then steps the job, and
R_WarmStep steps one nobody warms (a model the server alone uses) a step a frame. A job goes through
  HASH    worker: the GLM's tangent-cache key (it hashes every byte: ~20 ms for a 34 MB hero)
  FETCH   main:   its prepared tangents from memory or cache/tangents (R_TangentFetch, the FS)
  PACK    worker: the tangents computed when there were none (MikkTSpace: 0.4-0.7 s on a hero's first load ever),
                  then every LOD packed (R_PackMDXMLod)
  UPLOAD  main:   a LOD a step (R_UploadMDXMLod, through the upload ring); after the last one, computed tangents are
                  kept and written (R_TangentAdopt) and the model's vboModels published.
The packed bytes are the ones the one-step build makes. Until it is published vboModels stays NULL: a draw of the model
in the meantime (another player who picked it) completes the job at once (R_MeshJobFinish from the surface setup in
tr_ghoul2.cpp), on the main thread, which is what the build cost before. The worker touches only its job and the GLM,
which nothing writes after R_LoadMDXM; it makes no Printf, Hunk, FS or Vulkan calls.
*/
#include "tr_local.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <thread>

enum meshJobPhase_t { MJ_HASH, MJ_FETCH, MJ_PACK, MJ_UPLOAD };

struct meshJob_t {
	model_t				*mod;
	const mdxmHeader_t	*mdxm;
	mdxmVBOModel_t		*vboModels;		// published into mod once every LOD is uploaded
	meshJobPhase_t		phase;
	bool				busy;			// a thread is working on HASH or PACK
	bool				failed;			// the worker ran out of memory: the main thread builds it the old way
	uint64_t			key;
	bool				useCache;		// r_tangentCache when queued (the worker reads no cvar)
	bool				computed;		// the tangents were computed here (adopt them into the cache)
	std::vector<std::vector<float> >	tangents;	// per LOD; empty = none (pack computes per surface)
	std::vector<mdxmLodPack_t>			lods;
	int					nextLod;
	int					started;		// ri.Milliseconds()
	double				workMs;			// worker time
};

// The ownership rules: only the main thread calls the queue, step, finish and level-load entry points.
// `jobs` and every job's phase and busy flag change only under `lock`; the worker touches only a job it marked busy, and a
// job is erased (under the lock) only in a phase the worker never takes. `running`, `defer` and `levelLoad` are
// main-thread state; `quit` is set and read under the lock
static struct {
	std::vector<meshJob_t *>	jobs;
	std::mutex					lock;
	std::condition_variable		wake;		// the worker: a job to do, or quit
	std::condition_variable		done;		// the main thread: a job's phase changed
	std::thread					*worker;	// by pointer: an exit that skips the shutdown (Sys_Error) must not destroy a joinable std::thread (std::terminate)
	bool						running, quit, defer;
	bool						levelLoad = true;	// loading: no world scene rendered since the last registration or world load
} mj;

static cvar_t *r_meshWorker;

static meshJob_t *R_MeshJobFor( const model_t *mod ) {
	for ( size_t i = 0; i < mj.jobs.size(); i++ ) {
		if ( mj.jobs[i]->mod == mod ) return mj.jobs[i];
	}
	return NULL;
}

static bool R_MeshJobForWorker( const meshJob_t *j ) {
	return !j->busy && ( j->phase == MJ_HASH || j->phase == MJ_PACK );
}

// HASH or PACK, on whichever thread holds j->busy (no locks held)
static void R_MeshJobWorkInner( meshJob_t *j ) {
	if ( j->phase == MJ_HASH ) {
		j->key = R_TangentKeyOf( j->mdxm );
	} else {
		const int numLODs = j->mdxm->numLODs;
		if ( j->tangents.empty() && j->useCache ) {
			R_TangentComputeLods( j->mdxm, j->tangents );
			j->computed = true;
		}
		j->lods.resize( numLODs );
		for ( int l = 0; l < numLODs; l++ ) {
			const bool have = l < (int)j->tangents.size() && !j->tangents[l].empty()
				&& j->tangents[l].size() == (size_t)R_MdxmLodVerts( j->mdxm, l ) * 4;
			R_PackMDXMLod( j->mdxm, l, have ? &j->tangents[l][0] : NULL, j->lods[l] );
		}
		if ( !j->computed ) {
			std::vector<std::vector<float> >().swap( j->tangents );	// packed; only computed ones are kept
		}
	}
}

static void R_MeshJobWork( meshJob_t *j ) {
	const auto t0 = std::chrono::steady_clock::now();
	try {
		R_MeshJobWorkInner( j );
	} catch ( ... ) {	// any failure (not only bad_alloc) hands the model back to the main thread's one-step build
		std::vector<std::vector<float> >().swap( j->tangents );
		std::vector<mdxmLodPack_t>().swap( j->lods );
		j->computed = false;
		j->failed = true;
	}
	j->workMs += std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - t0 ).count();
}

static void R_MeshJobAdvance( meshJob_t *j ) {
	j->phase = j->failed || j->phase == MJ_PACK ? MJ_UPLOAD : MJ_FETCH;
}

static void R_MeshWorkerLoop( void ) {
	std::unique_lock<std::mutex> lk( mj.lock );
	for ( ;; ) {
		meshJob_t *j = NULL;
		mj.wake.wait( lk, [&j]() {
			if ( mj.quit ) return true;
			for ( size_t i = 0; i < mj.jobs.size() && !j; i++ ) {
				if ( R_MeshJobForWorker( mj.jobs[i] ) ) j = mj.jobs[i];
			}
			return j != NULL;
		} );
		if ( mj.quit ) {
			return;
		}
		j->busy = true;
		lk.unlock();
		R_MeshJobWork( j );
		lk.lock();
		R_MeshJobAdvance( j );
		j->busy = false;
		mj.done.notify_all();
	}
}

static void R_MeshJobsCvar( void ) {
	if ( !r_meshWorker ) {
		r_meshWorker = ri.Cvar_Get( "r_meshWorker", "1", 0, "Player models (a warm-up's, and any registered while a map runs): 1 pack the GLM on a worker thread and upload it a LOD a frame, 0 build it in one step" );
	}
}

void R_MeshJobsDefer( qboolean defer ) {
	R_MeshJobsCvar();
	mj.defer = defer && r_meshWorker->integer;
}

void R_MeshJobsLevelLoad( qboolean loading ) {
	mj.levelLoad = loading ? true : false;
}

qboolean R_MeshJobQueue( model_t *mod, mdxmHeader_t *mdxm ) {
	if ( !mod || !mdxm || mdxm->numLODs < 1 ) {
		return qfalse;
	}
	R_MeshJobsCvar();
	if ( !mj.defer && ( !r_meshWorker->integer || mj.levelLoad || Q_stricmpn( mod->name, "models/players/", 15 ) ) ) {
		return qfalse;
	}
	meshJob_t *j = new meshJob_t();
	j->mod = mod;
	j->mdxm = mdxm;
	j->vboModels = (mdxmVBOModel_t *)ri.Hunk_Alloc( sizeof( mdxmVBOModel_t ) * mdxm->numLODs, h_low );
	Com_Memset( j->vboModels, 0, sizeof( mdxmVBOModel_t ) * mdxm->numLODs );
	j->phase = MJ_HASH;
	j->busy = false;
	j->failed = false;
	j->key = 0;
	j->useCache = r_tangentCache && r_tangentCache->integer;
	j->computed = false;
	j->nextLod = 0;
	j->started = ri.Milliseconds();
	j->workMs = 0.0;
	mod->data.glm->vboModels = NULL;
	{
		std::lock_guard<std::mutex> lk( mj.lock );
		mj.jobs.push_back( j );
	}
	if ( !mj.running ) {
		mj.quit = false;
		mj.worker = new std::thread( R_MeshWorkerLoop );
		mj.running = true;
	}
	mj.wake.notify_one();
	return qtrue;
}

// a main-thread step of a job not being worked on (FETCH, or one LOD's UPLOAD); false when it finished the job
static bool R_MeshJobMainStep( meshJob_t *j ) {
	if ( j->phase == MJ_FETCH ) {
		R_TangentFetch( j->mdxm, j->key, j->tangents );		// a miss leaves them empty: PACK computes them
		{
			std::lock_guard<std::mutex> lk( mj.lock );
			j->phase = MJ_PACK;
		}
		mj.wake.notify_one();
		return true;
	}
	// MJ_UPLOAD
	if ( j->failed && (int)j->lods.size() <= j->nextLod ) {	// out of memory on the worker: a LOD at a time here
		j->lods.resize( j->nextLod + 1 );
		R_PackMDXMLod( j->mdxm, j->nextLod, R_MdxmTangents( j->mdxm, j->nextLod, R_MdxmLodVerts( j->mdxm, j->nextLod ) ),
			j->lods[j->nextLod] );
	}
	R_UploadMDXMLod( j->mdxm, j->nextLod, j->lods[j->nextLod], &j->vboModels[j->nextLod] );
	std::vector<byte>().swap( j->lods[j->nextLod].data );	// its memory back now (a hero's LOD 0 is tens of MB)
	std::vector<uint32_t>().swap( j->lods[j->nextLod].indices );
	if ( ++j->nextLod < j->mdxm->numLODs ) {
		return true;
	}
	if ( j->computed ) {
		R_TangentAdopt( j->mdxm, j->key, j->tangents );
	}
	j->mod->data.glm->vboModels = j->vboModels;
	if ( j->failed ) ri.Printf( PRINT_WARNING, "mesh worker: %s: out of memory, built on the main thread\n", j->mdxm->name );
	ri.Printf( PRINT_DEVELOPER, "mesh worker: %s: %d LODs in %d ms (%.0f ms on the worker%s)\n", j->mdxm->name, j->mdxm->numLODs,
		ri.Milliseconds() - j->started, j->workMs, j->computed ? ", tangents computed" : "" );
	{
		std::lock_guard<std::mutex> lk( mj.lock );
		for ( size_t i = 0; i < mj.jobs.size(); i++ ) {
			if ( mj.jobs[i] == j ) {
				mj.jobs.erase( mj.jobs.begin() + i );
				break;
			}
		}
	}
	delete j;
	return false;
}

int R_MeshJobStep( const model_t *mod ) {
	meshJob_t *j;
	{
		std::lock_guard<std::mutex> lk( mj.lock );
		j = R_MeshJobFor( mod );
		if ( !j ) {
			return -1;
		}
		if ( j->busy || j->phase == MJ_HASH || j->phase == MJ_PACK ) {
			return 0;
		}
	}
	R_MeshJobMainStep( j );
	return 1;
}

// R_WarmStep with no warm-up running: a step of the oldest job waiting for the main thread (one no warm-up steps)
void R_MeshJobStepAny( void ) {
	meshJob_t *j = NULL;
	{
		std::lock_guard<std::mutex> lk( mj.lock );
		for ( size_t i = 0; i < mj.jobs.size() && !j; i++ ) {
			if ( !mj.jobs[i]->busy && ( mj.jobs[i]->phase == MJ_FETCH || mj.jobs[i]->phase == MJ_UPLOAD ) ) j = mj.jobs[i];
		}
	}
	if ( j ) {
		R_MeshJobMainStep( j );
	}
}

void R_MeshJobFinish( const model_t *mod ) {
	for ( ;; ) {
		meshJob_t *j;
		{
			std::unique_lock<std::mutex> lk( mj.lock );
			j = R_MeshJobFor( mod );
			if ( !j ) {
				return;
			}
			if ( j->busy ) {
				// past 2 s the wait names the model and its phase (a 13 s main-thread stall was seen once, cause not found)
				for ( int waited = 0; !mj.done.wait_for( lk, std::chrono::seconds( 2 ), [j]() { return !j->busy; } ); ) {
					waited += 2;
					ri.Printf( PRINT_WARNING, "mesh worker: %s: still %s after %d s\n", j->mdxm->name,
						j->phase == MJ_HASH ? "hashing" : "packing", waited );
				}
				continue;
			}
			if ( j->phase == MJ_HASH || j->phase == MJ_PACK ) {
				j->busy = true;	// this thread does it, rather than wait for the worker to come to it
				lk.unlock();
				R_MeshJobWork( j );
				lk.lock();
				R_MeshJobAdvance( j );
				j->busy = false;
				mj.done.notify_all();
				continue;
			}
		}
		if ( !R_MeshJobMainStep( j ) ) {
			return;
		}
	}
}

void R_MeshJobsShutdown( void ) {
	if ( mj.running ) {
		{
			std::lock_guard<std::mutex> lk( mj.lock );
			mj.quit = true;
		}
		mj.wake.notify_all();
		mj.worker->join();	// it finishes the step it is on: nothing it touches is freed before this
		delete mj.worker;
		mj.worker = NULL;
		mj.running = false;
	}
	for ( size_t i = 0; i < mj.jobs.size(); i++ ) {
		delete mj.jobs[i];	// their models go with the map; their vboModels were never published
	}
	mj.jobs.clear();
	mj.quit = false;
	mj.defer = false;
	mj.levelLoad = true;	// until the next world scene
}
