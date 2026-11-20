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
Player model warm-up (docs/character-loading-plan.md, M2).

A model change used to load everything in one frame, on a listen server inside the server's frame (the game module's
skin registration hands over to RE_RegisterSkin). R_WarmModel takes a "model[/skin]" userinfo value and, a step at a
time from RE_BeginFrame under r_modelWarmupBudget:
  1. reads the model's skins (the asked one, red, blue) for their shader names, and has the engine read every pk3
     entry in the model's folder and in the folders those shaders' files live in into the OS cache (FS_PrefetchDir,
     a worker thread);
  2. once those reads are done, registers the GLM (RE_RegisterModel), whose LODs the mesh worker packs meanwhile
     (vk_mesh_jobs.cpp), then loads and binds its cloth and hair assets;
  3. registers the skins' shaders one at a time (R_FindShader, as RE_RegisterSkin calls it: their images);
  4. asks the pipeline worker for the pipelines the model's draws will bind (vk_pipeline_async.cpp; the shaders' own
     creation compiled none), uploads the packed LODs a step each, builds the hair's substitute
     buffers a LOD a step and waits, without a step, for those pipelines;
  5. registers the skins.
The client holds the userinfo until the handle is ready (cl_modelWarm), so the server's and cgame's own registrations
find everything loaded. A step cannot be split: the largest is one shader with its textures.

With texture streaming (vk_stream.cpp, M3) the read-ahead takes only the head and tail of a streamed folder's DDS
files, a shader step loads its textures' mip tails, and the warmed model's textures are pinned at full detail: they
stream up after the handle is ready.

Other players (M2b): cgame warms another player's new model with WARMMODEL_OTHER_PLAYER (R_WarmModelEx) and draws
their old one until it is ready. Such a job pins nothing and warms only the skin asked for (cgame asks for the team
skin it will use); another such request for the same model and skin (working, or ready and still kept) shares it, and
one whose GLM is built and whose skin is registered is ready at once. The local player's requests never share: each
one pins its textures afresh.
*/
#include "tr_local.h"
#include "rd-common/tr_cloth.h"
#include "rd-common/tr_hair.h"
#include "tr_cache.h"

#include <string>
#include <vector>

enum warmStage_t { WARM_START, WARM_READ, WARM_MODEL, WARM_CLOTH, WARM_HAIR, WARM_SHADERS, WARM_PIPELINES, WARM_MESH, WARM_HAIRGPU, WARM_PIPEWAIT, WARM_SKINS, WARM_READY, WARM_FAILED };

struct warmJob_t {
	int							handle;
	warmStage_t					stage;
	std::string					model;		// folder under models/players
	std::vector<std::string>	skins;		// .skin paths that exist
	std::vector<std::string>	shaders;	// their shader names, once each
	std::vector<int>			prefetch;	// FS_PrefetchDir ids
	std::vector<const shader_t *>	found;	// the shaders registered
	std::vector<std::pair<uint32_t, int> >	pipelines;	// asked of the pipeline worker: (vk.pipelines index, pass)
	size_t						next;		// next shader
	size_t						pinCount;	// the first shaders are pinned: those of the skin asked for (all with r_streamPinTeamSkins)
	qhandle_t					glm;			// the GLM's handle, once registered
	std::string					asked;		// "model/skin" as requested (sharing)
	bool						pin;		// the local player's: its textures pinned at full detail
	int							started;	// ri.Milliseconds()
	int							frames;
	int							readSince;	// when WARM_READ began (ri.Milliseconds())
};

static std::vector<warmJob_t>	s_warm;
static int						s_warmNext;
static int						s_warmPinNewest;	// the local player's newest request (only it takes the stream pin)

#define WARM_READ_LIMIT_MS	10000	// a read-ahead that never reports done holds its job this long at most

#define WARM_KEEP	8	// finished jobs kept for WarmState

static bool R_WarmHaveApi( void ) {
	return ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) && ri.FS_PrefetchDir && ri.FS_PrefetchDone;
}

// a folder's read-ahead: streamed DDS files only for what loads at once (header and tail)
static int R_WarmPrefetch( const std::string &dir ) {
	if ( R_StreamPath( dir.c_str() ) && ri.Cvar_VariableIntegerValue( "fs_prefetchApi" ) >= 2 && ri.FS_PrefetchDirEx ) {
		return ri.FS_PrefetchDirEx( dir.c_str(), ".dds", 4096, R_StreamTailBytes() );
	}
	return ri.FS_PrefetchDir( dir.c_str() );
}

// an other player's request: everything it would register already is (the GLM with its mesh built, every skin)
static bool R_WarmLoaded( const warmJob_t &job ) {
	const qhandle_t h = CModelCache->GetModelHandle( ( "models/players/" + job.model + "/model.glm" ).c_str() );
	const model_t *mod = h > 0 ? R_GetModelByHandle( h ) : NULL;
	if ( !mod || mod->type != MOD_MDXM || !mod->data.glm || !mod->data.glm->vboModels ) {
		return false;
	}
	for ( size_t i = 0; i < job.skins.size(); i++ ) {
		if ( !R_SkinRegistered( job.skins[i].c_str() ) && ri.FS_ReadFile( job.skins[i].c_str(), NULL ) > 0 ) {
			return false;	// a skin file not registered yet (a missing one is skipped by the warm-up too)
		}
	}
	return true;
}

int R_WarmModel( const char *modelAndSkin ) {
	return R_WarmModelEx( modelAndSkin, 0 );
}

int R_WarmModelEx( const char *modelAndSkin, int flags ) {
	char model[MAX_QPATH], skin[MAX_QPATH];
	if ( !modelAndSkin || !modelAndSkin[0] || !tr.registered ) {
		return 0;
	}
	Q_strncpyz( model, modelAndSkin, sizeof( model ) );
	char *slash = strchr( model, '/' );
	Q_strncpyz( skin, slash && slash[1] ? slash + 1 : "default", sizeof( skin ) );
	if ( slash ) {
		*slash = 0;
	}
	if ( !model[0] ) {
		return 0;
	}
	const bool pin = !( flags & WARMMODEL_OTHER_PLAYER );
	const std::string asked = std::string( model ) + "/" + skin;
	for ( size_t i = 0; i < s_warm.size() && !pin; i++ ) {
		if ( s_warm[i].stage != WARM_FAILED && !s_warm[i].pin && !Q_stricmp( s_warm[i].asked.c_str(), asked.c_str() ) ) {
			return s_warm[i].handle;
		}
	}
	warmJob_t job;
	job.handle = ++s_warmNext;
	job.stage = WARM_START;
	job.model = model;
	job.next = 0;
	job.pinCount = 0;
	job.glm = 0;
	job.asked = asked;
	job.pin = pin;
	job.started = ri.Milliseconds();
	job.frames = 0;
	job.readSince = 0;
	if ( pin ) s_warmPinNewest = job.handle;
	// the skin asked for, then the team skins cgame and the server pick in team games; three-part skins ('|') are
	// built by RE_RegisterSkin from their parts, so only the model is warmed for them
	if ( !strchr( skin, '|' ) ) {
		const char *names[3] = { skin, "red", "blue" };
		for ( int i = 0; i < ( pin ? 3 : 1 ); i++ ) {
			if ( i && !Q_stricmp( names[i], skin ) ) continue;
			job.skins.push_back( va( "models/players/%s/model_%s.skin", model, names[i] ) );
		}
	}
	if ( !pin && R_WarmLoaded( job ) ) {
		job.stage = WARM_READY;
	}
	s_warm.push_back( job );
	ri.Printf( PRINT_DEVELOPER, "model warm-up %d: %s (skin %s)%s%s\n", job.handle, model, skin, pin ? "" : ", another player",
		job.stage == WARM_READY ? ", already loaded" : "" );
	return job.handle;
}

int R_WarmState( int handle ) {
	for ( size_t i = 0; i < s_warm.size(); i++ ) {
		if ( s_warm[i].handle == handle ) {
			return s_warm[i].stage == WARM_READY ? 1 : s_warm[i].stage == WARM_FAILED ? -1 : 0;
		}
	}
	return -1;
}

void R_WarmModel_f( void ) {
	if ( ri.Cmd_Argc() < 2 ) {
		ri.Printf( PRINT_ALL, "usage: warmmodel <model[/skin]>\n" );
		return;
	}
	ri.Printf( PRINT_ALL, "warmmodel: handle %d\n", R_WarmModel( ri.Cmd_Argv( 1 ) ) );
}

void R_WarmClear( void ) {
	s_warm.clear();
}

static void R_WarmAddUnique( std::vector<std::string> &list, const std::string &s ) {
	for ( size_t i = 0; i < list.size(); i++ ) {
		if ( !Q_stricmp( list[i].c_str(), s.c_str() ) ) return;
	}
	list.push_back( s );
}

// one step of a job; false when the job waits (nothing to do this frame)
static bool R_WarmJobStep( warmJob_t &j ) {
	switch ( j.stage ) {
	case WARM_START: {
		std::vector<std::string> skins, dirs;
		dirs.push_back( "models/players/" + j.model + "/" );
		// the voice cgame registers (CG_LoadCISounds): sound/chars/<the sounds.cfg path, else the model>/
		{
			char *cfg = NULL;
			std::string voice = j.model;
			const long len = ri.FS_ReadFile( va( "models/players/%s/sounds.cfg", j.model.c_str() ), (void **)&cfg );
			if ( len > 0 && cfg ) {
				size_t n = 0;
				while ( n < (size_t)len && cfg[n] && cfg[n] != '\r' && cfg[n] != '\n' ) n++;
				if ( n ) voice.assign( cfg, n );
				ri.FS_FreeFile( cfg );
			}
			dirs.push_back( "sound/chars/" + voice + "/" );
		}
		for ( size_t i = 0; i < j.skins.size(); i++ ) {
			std::vector<std::string> names;
			if ( R_SkinShaderNames( j.skins[i].c_str(), names ) ) {
				skins.push_back( j.skins[i] );
				for ( size_t n = 0; n < names.size(); n++ ) R_WarmAddUnique( j.shaders, names[n] );
			}
			if ( i == 0 ) j.pinCount = j.shaders.size();	// the skin asked for is the first
		}
		if ( !j.pinCount || R_StreamPinTeamSkins() ) j.pinCount = j.shaders.size();
		j.skins = skins;
		for ( size_t i = 0; i < j.shaders.size(); i++ ) {
			std::vector<std::string> d;
			R_ShaderTextDirs( j.shaders[i].c_str(), d );
			for ( size_t k = 0; k < d.size(); k++ ) R_WarmAddUnique( dirs, d[k] );
		}
		if ( R_WarmHaveApi() ) {
			for ( size_t i = 0; i < dirs.size(); i++ ) {
				const int id = R_WarmPrefetch( dirs[i] );
				if ( id ) j.prefetch.push_back( id );
			}
		}
		ri.Printf( PRINT_DEVELOPER, "model warm-up %d: %d skins, %d shaders, %d folders read ahead\n", j.handle,
			(int)j.skins.size(), (int)j.shaders.size(), (int)j.prefetch.size() );
		if ( j.pin && j.handle == s_warmPinNewest ) {
			// this model's textures, the previous one's no longer. An older request the player moved past (runs it
			// after the newest) warms without taking the pin back
			R_StreamPinBegin( j.handle );
		}
		j.stage = WARM_READ;
		j.readSince = ri.Milliseconds();
		return true;
	}
	case WARM_READ:
		for ( size_t i = 0; i < j.prefetch.size(); i++ ) {
			if ( ri.FS_PrefetchDone( j.prefetch[i] ) ) continue;
			if ( ri.Milliseconds() - j.readSince < WARM_READ_LIMIT_MS ) return false;
			ri.Printf( PRINT_DEVELOPER, "model warm-up %d: read-ahead not done after %d ms, going on\n", j.handle, WARM_READ_LIMIT_MS );
			break;
		}
		j.stage = WARM_MODEL;
		return true;
	case WARM_MODEL:
		R_MeshJobsDefer( qtrue );	// its LODs packed on the mesh worker, uploaded in WARM_MESH
		j.glm = RE_RegisterModel( ( "models/players/" + j.model + "/model.glm" ).c_str() );
		R_MeshJobsDefer( qfalse );
		if ( !j.glm ) {
			ri.Printf( PRINT_DEVELOPER, "model warm-up %d: %s has no model.glm\n", j.handle, j.model.c_str() );
			j.stage = WARM_FAILED;
			return true;
		}
		j.stage = WARM_CLOTH;
		return true;
	case WARM_CLOTH:	// the garment and hair assets the first draw would otherwise parse and bind
	case WARM_HAIR: {
		const model_t *mod = R_GetModelByHandle( j.glm );
		if ( j.stage == WARM_CLOTH ) {
			R_ClothWarmModel( mod );
			j.stage = WARM_HAIR;
		} else {
			R_HairWarmModel( mod );
			j.stage = WARM_SHADERS;
		}
		return true;
	}

	case WARM_SHADERS:
		if ( j.next < j.shaders.size() ) {
			vk_pipeline_defer( qtrue );	// no pipeline compiled here: WARM_PIPELINES asks for the ones the draws bind
			const size_t index = j.next;
			shader_t *sh = R_FindShader( j.shaders[j.next++].c_str(), lightmapsNone, stylesDefault, qtrue );
			vk_pipeline_defer( qfalse );
			if ( j.pin && index < j.pinCount ) {	// (the team skins' own textures stream by on-screen size)
				R_StreamPinShader( sh, j.handle );
			}
			if ( sh && sh != tr.defaultShader ) j.found.push_back( sh );
			return true;
		}
		j.stage = WARM_PIPELINES;
		return true;
	case WARM_PIPELINES:	// what the model's draws will bind, compiled on the worker meanwhile
		for ( size_t i = 0; i < j.found.size(); i++ ) vk_request_ghoul2_pipelines( j.found[i], j.pipelines );
		j.stage = WARM_MESH;
		return true;
	case WARM_MESH: {	// the GLM's LODs uploaded a step each once the mesh worker packed them
		const int r = R_MeshJobStep( R_GetModelByHandle( j.glm ) );
		if ( r == 0 ) return false;
		if ( r < 0 ) j.stage = WARM_HAIRGPU;
		return true;
	}
	case WARM_HAIRGPU:	// the hair substitute buffers, a LOD a step (they copy the uploaded mesh's packing)
		if ( R_HairWarmModelGpu( R_GetModelByHandle( j.glm ) ) ) j.stage = WARM_PIPEWAIT;
		return true;
	case WARM_PIPEWAIT:	// ready only once its pipelines are (a first-ever compile is 100-200 ms each)
		while ( !j.pipelines.empty() ) {
			const std::pair<uint32_t, int> &p = j.pipelines.back();
			if ( !vk.pipelines[p.first].handle[p.second] && vk_pipeline_inflight( p.first, (renderPass_t)p.second ) ) {
				return false;
			}
			j.pipelines.pop_back();	// installed, or dropped by a restart (a draw compiles it then)
		}
		j.stage = WARM_SKINS;
		return true;
	case WARM_SKINS:
		for ( size_t i = 0; i < j.skins.size(); i++ ) RE_RegisterSkin( j.skins[i].c_str() );
		j.stage = WARM_READY;
		ri.Printf( PRINT_DEVELOPER, "model warm-up %d: %s ready in %d ms, %d frames\n", j.handle, j.model.c_str(),
			ri.Milliseconds() - j.started, j.frames );
		return true;
	default:
		return false;
	}
}

// RE_BeginFrame: unfinished jobs' steps while the frame's budget lasts (one step at least)
void R_WarmStep( void ) {
	vk_pipeline_adopt();	// pipelines the worker finished, into their slots (vk_pipeline_async.cpp)
	// the local player's newest request first (the client holds its userinfo for that handle; earlier picks
	// the player moved past finish afterwards), then other players' in the order asked, then those earlier picks.
	// a job that waits (read-ahead, mesh worker, pipelines) no longer holds the others: the next one steps
	static std::vector<warmJob_t *> order;
	order.clear();
	for ( size_t i = s_warm.size(); i-- > 0; ) {
		if ( s_warm[i].pin && s_warm[i].handle == s_warmPinNewest && s_warm[i].stage != WARM_READY && s_warm[i].stage != WARM_FAILED ) order.push_back( &s_warm[i] );
	}
	for ( size_t i = 0; i < s_warm.size(); i++ ) {
		if ( !s_warm[i].pin && s_warm[i].stage != WARM_READY && s_warm[i].stage != WARM_FAILED ) order.push_back( &s_warm[i] );
	}
	for ( size_t i = s_warm.size(); i-- > 0; ) {
		if ( s_warm[i].pin && s_warm[i].handle != s_warmPinNewest && s_warm[i].stage != WARM_READY && s_warm[i].stage != WARM_FAILED ) order.push_back( &s_warm[i] );
	}
	const double t0 = R_SunProfNow();
	const double budget = r_modelWarmupBudget ? r_modelWarmupBudget->value : 6.0f;
	bool progressed = false;
	for ( size_t k = 0; k < order.size() && ( !progressed || R_SunProfNow() - t0 < budget ); k++ ) {
		warmJob_t *j = order[k];
		j->frames++;
		do {
			static const char *names[] = { "start", "read", "model", "cloth", "hair", "shader", "pipelines", "mesh", "hair buffer", "pipeline wait", "skins", "ready", "failed" };
			const warmStage_t stage = j->stage;
			const size_t shader = j->next;
			const double s0 = R_SunProfNow();
			if ( !R_WarmJobStep( *j ) ) break;
			progressed = true;
			const double ms = R_SunProfNow() - s0;
			if ( ms > 20.0 ) {	// the steps a frame cannot hide
				ri.Printf( PRINT_DEVELOPER, "model warm-up %d: %s step %s%.0f ms\n", j->handle, names[stage],
					stage == WARM_SHADERS && shader < j->shaders.size() ? va( "%s ", j->shaders[shader].c_str() ) : "", ms );
			}
		} while ( j->stage != WARM_READY && j->stage != WARM_FAILED && R_SunProfNow() - t0 < budget );
	}
	if ( !progressed ) {
		R_MeshJobStepAny();	// a player model the server alone registered (M2b): its LODs uploaded a step a frame
	}
	// keep a few finished jobs for WarmState, drop the oldest
	size_t finished = 0;
	for ( size_t i = 0; i < s_warm.size(); i++ ) {
		if ( s_warm[i].stage == WARM_READY || s_warm[i].stage == WARM_FAILED ) finished++;
	}
	for ( size_t i = 0; i < s_warm.size() && finished > WARM_KEEP; ) {
		if ( s_warm[i].stage == WARM_READY || s_warm[i].stage == WARM_FAILED ) {
			s_warm.erase( s_warm.begin() + i );
			finished--;
		} else {
			i++;
		}
	}
}
