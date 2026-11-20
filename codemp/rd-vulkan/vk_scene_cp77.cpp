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
vk_scene_cp77.cpp: the render scene's CP2077 side files (tools/levelport/cp2gltf.py and friends): maps/<map>.xfog
haze volumes, maps/<map>.signals traffic signals, maps/<map>.xrot rotating meshes (with their compute pass) and
maps/<map>.xscr screens. Moved from vk_scene.cpp.
*/
#include "vk_scene_local.h"

// the haze's local volumes, maps/<map>.xfog (cp2gltf.scene_fogs): "XFOG", version 1, count, pad; then per volume the
// 3x4 map from scene space to its [-1, 1] box and (density per metre, mode 0 a multiple of the haze's / 1 absolute,
// soft edge share, kind 0 box / 1 steam column). None: one zeroed record, so the set always has a buffer
typedef struct { char ident[4]; uint32_t version, count, pad; } xfogHeader_t;

// maps/<map>.signals (cp2gltf SceneSignals; docs/levelport-scene.md): "xsig 1 <count>", optional "sound <role> <path>"
// lines (the cgame's crossing sounds: skipped here, files with or without them read the same), then per slot "<slot>
// <crossing|car> <cycle> <offset> <x y z> <n> <G|Y|R> <seconds> ...". None: no signals (their lamps lit as built)
void Scene_LoadSignals( const char *scnName )
{
	char name[MAX_QPATH];
	char *buf = NULL;

	sc.numSignals = 0;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".signals" );
	const int len = ri.FS_ReadFile( name, (void **)&buf );
	if ( len <= 0 || !buf )
		return;
	std::string text( buf, len );
	ri.FS_FreeFile( buf );
	const char *at = text.c_str();
	char kind[16], c[SCENE_SIGNAL_SEGS][4];
	int version = 0, count = 0, used = 0;
	if ( sscanf( at, "xsig %d %d", &version, &count ) != 2 || version != 1 ) {	// (count: the header's; the lines are read to the end)
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 signal file, ignored\n", name );
		return;
	}
	for ( const char *line = strchr( at, '\n' ); line; line = strchr( line + 1, '\n' ) ) {
		int slot, n, k;
		float cycle, offset, x, y, z;
		if ( !Q_strncmp( line + 1, "sound ", 6 ) )
			continue;	// cg_signals.c's
		if ( sscanf( line + 1, "%d %15s %f %f %f %f %f %d%n", &slot, kind, &cycle, &offset, &x, &y, &z, &n, &used ) != 8
			|| slot < 1 || slot >= SCENE_MAX_SIGNALS || n < 1 || cycle <= 0.0f )
			continue;
		sceneSignal_t *S = &sc.signals[slot];
		Com_Memset( S, 0, sizeof( *S ) );
		S->crossing = !Q_stricmp( kind, "crossing" ) ? qtrue : qfalse;
		S->cycle = cycle;
		S->offset = offset;
		const char *seg = line + 1 + used;
		for ( k = 0; k < n && k < SCENE_SIGNAL_SEGS; k++ ) {
			float sec;
			if ( sscanf( seg, " %3s %f%n", c[k], &sec, &used ) != 2 )
				break;
			seg += used;
			S->colour[k] = c[k][0] == 'G' ? 0 : c[k][0] == 'Y' ? 1 : 2;
			S->seconds[k] = sec;
		}
		S->numSegs = k;
		sc.numSignals = MAX( sc.numSignals, slot + 1 );
	}
	ri.Printf( PRINT_ALL, "scene signals %s: %d\n", name, sc.numSignals ? sc.numSignals - 1 : 0 );
}

// the game's blink_01 curve (generic.curveset), a 1 s period: up over a quarter, held, down, off
static float Scene_Blink( float t )
{
	const float u = t - floorf( t );
	return u < 0.25f ? u * 4.0f : u < 0.5f ? 1.0f : u < 0.75f ? 1.0f - ( u - 0.5f ) * 4.0f : 0.0f;
}

// each signal's lamps at the eye view's game time: car lights one lamp of their group's colour; a crossing its walking
// figure on green, its hand blinking on yellow and steady on red; slot 0 (none) green
void Scene_SignalStates( float out[SCENE_MAX_SIGNALS][4] )
{
	Com_Memset( out, 0, sizeof( float ) * 4 * SCENE_MAX_SIGNALS );
	out[0][2] = 1.0f;
	const double now = sc.signalTime * 0.001 + r_sceneSignalShift->value;	// as cg_signals.c: a float of seconds steps
	for ( int i = 1; i < sc.numSignals; i++ ) {
		const sceneSignal_t *S = &sc.signals[i];
		if ( !r_sceneSignals->integer || !S->numSegs ) {
			out[i][0] = out[i][1] = out[i][2] = r_sceneSignals->integer ? 0.0f : 1.0f;
			if ( r_sceneSignals->integer )
				out[i][2] = 1.0f;
			continue;
		}
		float t = (float)fmod( r_sceneSignalAt->value >= 0.0f ? (double)r_sceneSignalAt->value : now + S->offset, S->cycle );
		if ( t < 0.0f )
			t += S->cycle;
		int k = 0;
		while ( k < S->numSegs - 1 && t >= S->seconds[k] ) {
			t -= S->seconds[k];
			k++;
		}
		const int colour = S->colour[k];
		if ( colour == 0 )
			out[i][2] = 1.0f;
		else if ( colour == 1 )
			out[i][S->crossing ? 0 : 1] = S->crossing ? Scene_Blink( t ) : 1.0f;
		else
			out[i][0] = 1.0f;
	}
}

qboolean Scene_LoadFogVolumes( const char *scnName )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	std::vector<float> recs( 16, 0.0f );

	sc.numFogVolumes = 0;
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xfog" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len >= (int)sizeof( xfogHeader_t ) ) {
		const xfogHeader_t *h = (const xfogHeader_t *)buf;
		if ( !memcmp( h->ident, "XFOG", 4 ) && h->version == 1 && h->count <= 4096
			&& (uint64_t)len >= sizeof( xfogHeader_t ) + (uint64_t)h->count * 64u ) {
			sc.numFogVolumes = h->count;
			if ( h->count ) {
				recs.resize( h->count * 16u );
				Com_Memcpy( recs.data(), (const byte *)buf + sizeof( xfogHeader_t ), h->count * 64u );
			}
		} else {
			ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 haze volume file, ignored\n", name );
		}
	}
	if ( buf )
		ri.FS_FreeFile( buf );
	{	// r_sceneFx: with the .xfx's particles the steam columns (kind 1, the converter's stand-ins) are left out
		char fxName[MAX_QPATH];
		COM_StripExtension( scnName, fxName, sizeof( fxName ) );
		Q_strcat( fxName, sizeof( fxName ), ".xfx" );
		if ( sc.numFogVolumes && r_sceneFx->integer && !r_sceneFxColumns->integer && ri.FS_ReadFile( fxName, NULL ) > 0 ) {
			uint32_t kept = 0;
			for ( uint32_t i = 0; i < sc.numFogVolumes; i++ ) {
				if ( recs[i * 16u + 15u] == 1.0f )
					continue;
				if ( kept != i )
					Com_Memcpy( &recs[kept * 16u], &recs[i * 16u], 64 );
				kept++;
			}
			if ( kept != sc.numFogVolumes )
				ri.Printf( PRINT_ALL, "scene haze volumes: %u steam columns left out for %s's particles (r_sceneFxColumns 0)\n", sc.numFogVolumes - kept, fxName );
			sc.numFogVolumes = kept;
			recs.resize( MAX( kept, 1u ) * 16u );
			if ( !kept )
				Com_Memset( recs.data(), 0, 64 );
		}
	}
	if ( sc.numFogVolumes )
		ri.Printf( PRINT_ALL, "scene haze volumes %s: %u\n", name, sc.numFogVolumes );
	// each volume's bounding sphere in scene space (scene_viewcull.comp): its box's centre p = M^-1 (0 - t) and the
	// farthest of its corners M^-1 (q - t), q in {-1, 1}^3; a map that cannot be inverted is kept everywhere
	std::vector<float> spheres( 4 * MAX( sc.numFogVolumes, 1u ), 0.0f );
	for ( uint32_t i = 0; i < sc.numFogVolumes; i++ ) {
		const float *r = &recs[i * 16];
		const float m[3][3] = { { r[0], r[1], r[2] }, { r[4], r[5], r[6] }, { r[8], r[9], r[10] } }, t[3] = { r[3], r[7], r[11] };
		const float det = m[0][0] * ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) - m[0][1] * ( m[1][0] * m[2][2] - m[1][2] * m[2][0] )
			+ m[0][2] * ( m[1][0] * m[2][1] - m[1][1] * m[2][0] );
		float *o = &spheres[i * 4];
		if ( fabsf( det ) < 1e-30f ) {
			o[3] = 1e30f;
			continue;
		}
		const float inv[3][3] = {
			{ ( m[1][1] * m[2][2] - m[1][2] * m[2][1] ) / det, ( m[0][2] * m[2][1] - m[0][1] * m[2][2] ) / det, ( m[0][1] * m[1][2] - m[0][2] * m[1][1] ) / det },
			{ ( m[1][2] * m[2][0] - m[1][0] * m[2][2] ) / det, ( m[0][0] * m[2][2] - m[0][2] * m[2][0] ) / det, ( m[0][2] * m[1][0] - m[0][0] * m[1][2] ) / det },
			{ ( m[1][0] * m[2][1] - m[1][1] * m[2][0] ) / det, ( m[0][1] * m[2][0] - m[0][0] * m[2][1] ) / det, ( m[0][0] * m[1][1] - m[0][1] * m[1][0] ) / det } };
		for ( int a = 0; a < 3; a++ )
			o[a] = -( inv[a][0] * t[0] + inv[a][1] * t[1] + inv[a][2] * t[2] );
		float r2 = 0.0f;
		for ( int k = 0; k < 8; k++ ) {
			const float q[3] = { ( k & 1 ) ? 1.0f : -1.0f, ( k & 2 ) ? 1.0f : -1.0f, ( k & 4 ) ? 1.0f : -1.0f };
			float d2 = 0.0f;
			for ( int a = 0; a < 3; a++ ) {
				const float d = inv[a][0] * q[0] + inv[a][1] * q[1] + inv[a][2] * q[2];
				d2 += d * d;
			}
			r2 = MAX( r2, d2 );
		}
		o[3] = sqrtf( r2 ) * 1.001f + 1.0f;
	}
	return ( Scene_StaticBuffer( &sc.fogVolumes, recs.data(), (VkDeviceSize)recs.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		"scene haze volumes" ) && Scene_StaticBuffer( &sc.volumeSpheres, spheres.data(), (VkDeviceSize)spheres.size() * 4,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene haze volume spheres" ) ) ? qtrue : qfalse;
}

// maps/<name>.xrot (tools/levelport): CP2077's rotating meshes (worldRotatingMeshNode: fans, propellers, the ad rings).
// This header, then count records of 36 bytes: the instance's index, then f32 its axis (the mesh's own frame, unit), its
// signed angular speed (rad/s), its pivot (the mesh's own frame, before the instance's scale) and a start phase (rad).
// Each frame the instance's matrix is its placement times the turn about the pivot (Scene_RotateFrame), copied into the
// instance buffer before the culls: every pass sees it turned, and its bounds turn with it
typedef struct { char ident[4]; uint32_t version, count, pad; } xrotHeader_t;
typedef struct { uint32_t inst; float m[12], axis[3], speed, pivot[3], phase; } sceneRot_t;
static std::vector<sceneRot_t> sceneRots;

qboolean Scene_LoadRotations( const char *scnName, const xscnInstance_t *inst, uint32_t numInst )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	float slowest = 0.0f, fastest = 0.0f;

	sceneRots.clear();
	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xrot" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len < (int)sizeof( xrotHeader_t ) ) {
		if ( buf )
			ri.FS_FreeFile( buf );
		return qtrue;
	}
	const xrotHeader_t *h = (const xrotHeader_t *)buf;
	if ( memcmp( h->ident, "XROT", 4 ) || h->version != 1 || (uint64_t)len < sizeof( xrotHeader_t ) + (uint64_t)h->count * 36u ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 rotation file, ignored\n", name );
		ri.FS_FreeFile( buf );
		return qtrue;
	}
	for ( uint32_t r = 0; r < h->count; r++ ) {
		const byte *rec = (const byte *)buf + sizeof( xrotHeader_t ) + r * 36u;
		sceneRot_t q;
		float f[8];
		Com_Memcpy( &q.inst, rec, 4 );
		Com_Memcpy( f, rec + 4, sizeof( f ) );
		const float n = sqrtf( f[0] * f[0] + f[1] * f[1] + f[2] * f[2] );
		if ( q.inst >= numInst || !( n >= 1e-6f ) )	// (a NaN axis too)
			continue;
		Com_Memcpy( q.m, inst[q.inst].m, sizeof( q.m ) );
		for ( int k = 0; k < 3; k++ ) {
			q.axis[k] = f[k] / n;
			q.pivot[k] = f[4 + k];
		}
		q.speed = f[3];
		q.phase = f[7];
		const float period = fabsf( q.speed ) > 1e-6f ? 2.0f * (float)M_PI / fabsf( q.speed ) : 0.0f;
		if ( period > 0.0f ) {
			slowest = MAX( slowest, period );
			fastest = fastest > 0.0f ? MIN( fastest, period ) : period;
		}
		sceneRots.push_back( q );
	}
	const uint32_t count = h->count;	// h lies in buf
	ri.FS_FreeFile( buf );
	ri.Printf( PRINT_ALL, "scene rotations %s: %u of %u records, turns of %.2g to %.3g s\n", name, (uint32_t)sceneRots.size(), count,
		fastest, slowest );
	if ( sceneRots.empty() )
		return qtrue;
	{	// scene_rotate.comp's records: M's rows, pivot and speed, axis and the instance (its bits)
		std::vector<float> params( sceneRots.size() * 20 );
		for ( size_t k = 0; k < sceneRots.size(); k++ ) {
			const sceneRot_t *q = &sceneRots[k];
			float *o = &params[k * 20];
			Com_Memcpy( o, q->m, 12 * sizeof( float ) );
			VectorCopy( q->pivot, o + 12 );
			o[15] = q->speed;
			VectorCopy( q->axis, o + 16 );
			Com_Memcpy( o + 19, &q->inst, 4 );
		}
		if ( !Scene_StaticBuffer( &sc.rotParams, params.data(), (VkDeviceSize)params.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "scene rotation records" ) )
			return qfalse;
	}
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		if ( !Scene_CreateBuffer( &sc.rotPatch[f], (VkDeviceSize)sceneRots.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, "scene rotations" ) )
			return qfalse;
		VK_CHECK( qvkMapMemory( vk.device, sc.rotPatch[f].mem, 0, VK_WHOLE_SIZE, 0, (void **)&sc.rotPtr[f] ) );
		sc.rotBased[f] = qfalse;
	}
	return qtrue;
}

// scene_rotate.comp's set layout, pipeline and sets (Scene_RotatePass, once)
static qboolean Scene_RotateCreate( void )
{
	static const VkDescriptorType types[3] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
	static const VkShaderStageFlags stages[3] = { VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT, VK_SHADER_STAGE_COMPUTE_BIT };
	sc.rotLayout = Scene_SetLayout( types, stages, 3 );
	if ( !sc.rotLayout )
		return qfalse;
	VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 32 };
	VkPipelineLayoutCreateInfo pl;
	Com_Memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 1;
	pl.pSetLayouts = &sc.rotLayout;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	if ( qvkCreatePipelineLayout( vk.device, &pl, NULL, &sc.rotPipeLayout ) != VK_SUCCESS )
		return qfalse;
	VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * NUM_COMMAND_BUFFERS };
	VkDescriptorPoolCreateInfo dp;
	Com_Memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.maxSets = NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 1;
	dp.pPoolSizes = &ps;
	if ( qvkCreateDescriptorPool( vk.device, &dp, NULL, &sc.rotPool ) != VK_SUCCESS )
		return qfalse;
	for ( int f = 0; f < NUM_COMMAND_BUFFERS; f++ ) {
		VkDescriptorSetAllocateInfo da;
		Com_Memset( &da, 0, sizeof( da ) );
		da.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		da.descriptorPool = sc.rotPool;
		da.descriptorSetCount = 1;
		da.pSetLayouts = &sc.rotLayout;
		if ( qvkAllocateDescriptorSets( vk.device, &da, &sc.rotSet[f] ) != VK_SUCCESS )
			return qfalse;
		const sceneBuffer_t *bufs[3] = { &sc.rotParams, &sc.rotPatch[f], &sc.instances };
		Scene_WriteSet( sc.rotSet[f], bufs, types, 3 );
	}
	VkShaderModule cs = Scene_Shader( scene_rotate_comp_spv, scene_rotate_comp_spv_size );
	if ( !cs )
		return qfalse;
	VkComputePipelineCreateInfo ci;
	Com_Memset( &ci, 0, sizeof( ci ) );
	ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	ci.stage.module = cs;
	ci.stage.pName = "main";
	ci.layout = sc.rotPipeLayout;
	const VkResult r = qvkCreateComputePipelines( vk.device, VK_NULL_HANDLE, 1, &ci, NULL, &sc.rotPipe );
	qvkDestroyShaderModule( vk.device, cs, NULL );
	return r == VK_SUCCESS ? qtrue : qfalse;
}

void Scene_RotateFree( void )
{
	if ( sc.rotPipe ) qvkDestroyPipeline( vk.device, sc.rotPipe, NULL );
	if ( sc.rotPipeLayout ) qvkDestroyPipelineLayout( vk.device, sc.rotPipeLayout, NULL );
	if ( sc.rotPool ) qvkDestroyDescriptorPool( vk.device, sc.rotPool, NULL );
	if ( sc.rotLayout ) qvkDestroyDescriptorSetLayout( vk.device, sc.rotLayout, NULL );
	sc.rotPipe = VK_NULL_HANDLE;
	sc.rotPipeLayout = VK_NULL_HANDLE;
	sc.rotPool = VK_NULL_HANDLE;
	sc.rotLayout = VK_NULL_HANDLE;
	Scene_DestroyBuffer( &sc.rotParams );
	sceneRots.clear();
}

// the pass, made on the first frame with rotating meshes; if any part fails, what was made is freed and the meshes stay
// as placed (not tried again every frame)
static qboolean Scene_RotatePass( void )
{
	if ( sc.rotPipe )
		return qtrue;
	if ( Scene_RotateCreate() )
		return qtrue;
	ri.Printf( PRINT_WARNING, "WARNING: scene rotations: their compute pass could not be made; the meshes stay as placed\n" );
	Scene_RotateFree();
	sceneRots.clear();
	return qfalse;
}

// each rotating instance's matrix now: its placement M times the turn about its pivot p (axis a, angle t * speed +
// phase): M [R | p - R p], turned in the instance buffer by scene_rotate.comp. This slot's phases are each instance's
// angle at the slot's time base, in double precision (fmod( t0 * speed, 2 pi ) + phase), written when the base is more
// than 20 s old or r_sceneRotate changed; the pass adds the seconds since. The slot's last frame is done: its phases are free
void Scene_RotateFrame( VkCommandBuffer cb, int f )
{
	if ( sceneRots.empty() || !sc.rotPtr[f] || !Scene_RotatePass() )
		return;
	const double t = (double)(uint32_t)sc.signalTime * 0.001;
	const int turn = r_sceneRotate->integer ? 1 : 0;
	if ( !sc.rotBased[f] || turn != sc.rotTurn[f] || t < sc.rotT0[f] || t - sc.rotT0[f] > 20.0 ) {
		for ( size_t k = 0; k < sceneRots.size(); k++ ) {
			const sceneRot_t *q = &sceneRots[k];
			sc.rotPtr[f][k] = (float)( ( turn ? fmod( t * q->speed, 2.0 * M_PI ) : 0.0 ) + q->phase );
		}
		sc.rotT0[f] = t;
		sc.rotTurn[f] = turn;
		sc.rotBased[f] = qtrue;
	}
	struct { uint32_t p[4]; float t[4]; } push = { { (uint32_t)sceneRots.size(), 0, 0, 0 }, { turn ? (float)( t - sc.rotT0[f] ) : 0.0f, 0, 0, 0 } };
	VkMemoryBarrier mb;
	Com_Memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	mb.srcAccessMask = 0;	// every earlier command's reads of the instances (the other slot's frame) before the writes
	mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
	qvkCmdBindPipeline( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.rotPipe );
	qvkCmdBindDescriptorSets( cb, VK_PIPELINE_BIND_POINT_COMPUTE, sc.rotPipeLayout, 0, 1, &sc.rotSet[f], 0, NULL );
	qvkCmdPushConstants( cb, sc.rotPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
	qvkCmdDispatch( cb, ( (uint32_t)sceneRots.size() + 63 ) / 64, 1, 1 );
	mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier( cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		0, 1, &mb, 0, NULL, 0, NULL );
}

// maps/<name>.xscr (tools/levelport, scene_map_files): this header, then count records of 68 bytes, one per screen
// material (an ad, a UI, a television, a parallax screen, a hologram): its index in the scene's materials, then 16 f32 as
// CP2077's material says them: template (1 television_ad, 2 metal_base_ui, 3 parallaxscreen, 4 its transparent kinds,
// 5 hologram), its flipbook's tiles across and down and frames per second; its pixel grid's rows, darkness, lines (0) or
// dots (1) and fade distance (m); its layers' separation (UV) and the second to fourth layers' strengths; its blink
// speed, hue cycle speed, broken share and additive share. Each record goes to the end of the detail buffer and its
// offset (floats) into the material's emissive.w, which scene.frag reads for a screen (info.w 1)
typedef struct { char ident[4]; uint32_t version, count, pad; } xscrHeader_t;

void Scene_LoadScreens( const char *scnName, std::vector<float> &params )
{
	char name[MAX_QPATH];
	void *buf = NULL;
	uint32_t kept = 0, flipbooks = 0, templates[6] = { 0 };

	COM_StripExtension( scnName, name, sizeof( name ) );
	Q_strcat( name, sizeof( name ), ".xscr" );
	const int len = ri.FS_ReadFile( name, &buf );
	if ( len < (int)sizeof( xscrHeader_t ) ) {
		if ( buf )
			ri.FS_FreeFile( buf );
		return;
	}
	const xscrHeader_t *h = (const xscrHeader_t *)buf;
	if ( memcmp( h->ident, "XSCR", 4 ) || h->version != 1 || (uint64_t)len < sizeof( xscrHeader_t ) + (uint64_t)h->count * 68u ) {
		ri.Printf( PRINT_WARNING, "WARNING: %s: not a version 1 screen file, ignored\n", name );
		ri.FS_FreeFile( buf );
		return;
	}
	for ( uint32_t r = 0; r < h->count; r++ ) {
		const byte *rec = (const byte *)buf + sizeof( xscrHeader_t ) + r * 68u;
		uint32_t i;
		float f[16];
		Com_Memcpy( &i, rec, 4 );
		Com_Memcpy( f, rec + 4, sizeof( f ) );
		if ( i >= sc.numMaterials || Q_isnan( f[0] ) )	// (its template indexes the counts)
			continue;
		sceneMaterial_t *m = &sc.materials[i];
		// a hologram's dots (template 5, dots on, a grid): a masked glow keeps its glow under info.w 64 (scene.frag)
		const qboolean holoDots = ( m->window == 4u && (int)( f[0] + 0.5f ) == 5 && f[6] > 0.5f && f[4] > 0.0f ) ? qtrue : qfalse;
		if ( ( m->kind != SCENE_KIND_EMISSIVE && m->kind != SCENE_KIND_BLEND ) || ( m->window != 0u && m->window != 1u && !holoDots ) )
			continue;	// a window, a masked glow or a signal's lamp: info.w is theirs
		if ( params.size() % 4u )
			params.resize( ( params.size() + 3u ) & ~(size_t)3u, 0.0f );
		m->window = holoDots ? 64u : 1u;
		if ( holoDots )
			sc.numHoloDots++;
		m->emissive[3] = (float)params.size();	// its record (scene.frag's screenParam); exact below 2^24 floats
		params.insert( params.end(), f, f + 16 );
		templates[(uint32_t)Com_Clamp( 0.0f, 5.0f, f[0] )]++;
		if ( MAX( floorf( f[1] + 0.5f ), 1.0f ) * MAX( floorf( f[2] + 0.5f ), 1.0f ) > 1.0f )
			flipbooks++;
		kept++;
	}
	const uint32_t count = h->count;	// h lies in buf
	ri.FS_FreeFile( buf );
	ri.Printf( PRINT_ALL, "scene screens %s: %u of %u records (television %u, ui %u, parallax %u, transparent %u, hologram %u), "
		"%u flipbooks\n", name, kept, count, templates[1], templates[2], templates[3], templates[4], templates[5], flipbooks );
}
