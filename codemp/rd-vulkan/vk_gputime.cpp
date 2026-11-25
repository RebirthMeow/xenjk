/*
===========================================================================
GPU time per render pass (bench instrumentation).

r_gpuTimes 1 writes a timestamp before every render pass begins and after it ends, reads them back when the
command buffer's fence has been waited (two frames later, no stall) and prints one line per frame:
	VKGPU <frame ms> | Pass <name>: <ms>ms Pass <name>: <ms>ms ...
Passes with the same name in one frame (blur levels, repeated main passes) are summed; a pass name's ": <n>"
suffix is dropped so "bloom blur pass: 3" counts as "bloom blur pass". Work between passes (copies, compute)
is in the frame total but in no pass. vk_gputime_next_label names the next pass instead of its render pass (one
render pass serving two jobs); vk_gputime_split closes the open pass and times the rest of it under another name
(e.g. the end of a render pass: its resolve and stores).
===========================================================================
*/

#include "tr_local.h"

#define GPUTIME_MAX_QUERIES		256
#define GPUTIME_MAX_PASSES		256

typedef struct {
	VkRenderPass	handle;
	char			name[48];
} gpuPassName_t;

typedef struct {
	const char		*name[GPUTIME_MAX_QUERIES / 2];
	int				numPasses;
	int				numQueries;
	qboolean		recorded;
} gpuFrameTimes_t;

static gpuPassName_t	passNames[GPUTIME_MAX_PASSES];
static int				numPassNames;
static VkQueryPool		pools[NUM_COMMAND_BUFFERS];
static gpuFrameTimes_t	frames[NUM_COMMAND_BUFFERS];
static float			nsPerTick;
static qboolean			active;	// this frame records timestamps
static const char		*nextLabel;	// vk_gputime_next_label

static PFN_vkCreateQueryPool		pCreateQueryPool;
static PFN_vkDestroyQueryPool		pDestroyQueryPool;
static PFN_vkCmdResetQueryPool		pCmdResetQueryPool;
static PFN_vkCmdWriteTimestamp		pCmdWriteTimestamp;
static PFN_vkGetQueryPoolResults	pGetQueryPoolResults;

void vk_gputime_register_pass( VkRenderPass handle, const char *name )
{
	if ( !name )
		return;
	// a recreated render pass can get a destroyed one's handle: its entry takes the new name (a stale first match
	// put old names on new passes), else the first free entry
	gpuPassName_t *p = NULL;
	for ( int i = 0; i < numPassNames && !p; i++ )
		if ( passNames[i].handle == handle )
			p = &passNames[i];
	for ( int i = 0; i < numPassNames && !p; i++ )
		if ( passNames[i].handle == VK_NULL_HANDLE )
			p = &passNames[i];
	if ( !p ) {
		if ( numPassNames >= GPUTIME_MAX_PASSES )
			numPassNames = 0;
		p = &passNames[numPassNames++];
	}
	p->handle = handle;
	Q_strncpyz( p->name, name, sizeof( p->name ) );
	char *colon = strchr( p->name, ':' );
	if ( colon )
		*colon = '\0';
}

// the render pass is being destroyed: its handle may come back for another pass
void vk_gputime_unregister_pass( VkRenderPass handle )
{
	for ( int i = 0; i < numPassNames; i++ )
		if ( passNames[i].handle == handle )
			passNames[i].handle = VK_NULL_HANDLE;
}

static const char *pass_name( VkRenderPass handle )
{
	for ( int i = 0; i < numPassNames; i++ )
		if ( passNames[i].handle == handle )
			return passNames[i].name;
	return "unnamed";
}

static qboolean init_pools( void )
{
	if ( pools[0] )
		return qtrue;

	pCreateQueryPool = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCreateQueryPool" );
	pDestroyQueryPool = (PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr( vk.device, "vkDestroyQueryPool" );
	pCmdResetQueryPool = (PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCmdResetQueryPool" );
	pCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)qvkGetDeviceProcAddr( vk.device, "vkCmdWriteTimestamp" );
	pGetQueryPoolResults = (PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr( vk.device, "vkGetQueryPoolResults" );
	if ( !pCreateQueryPool || !pCmdResetQueryPool || !pCmdWriteTimestamp || !pGetQueryPoolResults )
		return qfalse;

	VkPhysicalDeviceProperties props;
	qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
	if ( props.limits.timestampPeriod <= 0.0f )
		return qfalse;
	nsPerTick = props.limits.timestampPeriod;

	VkQueryPoolCreateInfo info;
	Com_Memset( &info, 0, sizeof( info ) );
	info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	info.queryType = VK_QUERY_TYPE_TIMESTAMP;
	info.queryCount = GPUTIME_MAX_QUERIES;
	for ( int i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( pCreateQueryPool( vk.device, &info, NULL, &pools[i] ) != VK_SUCCESS )
			return qfalse;
		frames[i].recorded = qfalse;
	}
	return qtrue;
}

static void print_results( int slot )
{
	gpuFrameTimes_t *f = &frames[slot];
	if ( !f->recorded || f->numQueries < 2 )
		return;

	uint64_t ticks[GPUTIME_MAX_QUERIES];
	if ( pGetQueryPoolResults( vk.device, pools[slot], 0, f->numQueries, sizeof( ticks ), ticks, sizeof( uint64_t ),
		VK_QUERY_RESULT_64_BIT ) != VK_SUCCESS )
		return;

	// sum by name, in first-seen order
	const char *names[GPUTIME_MAX_QUERIES / 2];
	double ms[GPUTIME_MAX_QUERIES / 2];
	int n = 0;
	for ( int p = 0; p < f->numPasses; p++ ) {
		double d = (double)( ticks[2 * p + 1] - ticks[2 * p] ) * nsPerTick / 1e6;
		int k = 0;
		while ( k < n && strcmp( names[k], f->name[p] ) != 0 )
			k++;
		if ( k == n ) {
			names[n] = f->name[p];
			ms[n++] = 0.0;
		}
		ms[k] += d;
	}
	double total = (double)( ticks[f->numQueries - 1] - ticks[0] ) * nsPerTick / 1e6;
	char line[2048];
	Com_sprintf( line, sizeof( line ), "VKGPU %.3f |", total );
	for ( int k = 0; k < n; k++ )
		Q_strcat( line, sizeof( line ), va( " Pass %s: %.3fms", names[k], ms[k] ) );
	ri.Printf( PRINT_ALL, "%s\n", line );
	if ( r_gpuTimes->integer < 2 )
		return;
	// r_gpuTimes 2: the time between each pass's end and the next one's begin (the untimed work there, barriers, waits),
	// summed by the two passes' names: VKGAP <sum> | Gap <before> > <after>: <ms>ms ...
	const char *ga[GPUTIME_MAX_QUERIES / 2], *gb[GPUTIME_MAX_QUERIES / 2];
	double gms[GPUTIME_MAX_QUERIES / 2], gsum = 0.0;
	int gn = 0;
	for ( int p = 1; p < f->numPasses; p++ ) {
		const double d = (double)( ticks[2 * p] - ticks[2 * p - 1] ) * nsPerTick / 1e6;
		gsum += d;
		int k = 0;
		while ( k < gn && ( strcmp( ga[k], f->name[p - 1] ) || strcmp( gb[k], f->name[p] ) ) )
			k++;
		if ( k == gn ) {
			ga[gn] = f->name[p - 1];
			gb[gn] = f->name[p];
			gms[gn++] = 0.0;
		}
		gms[k] += d;
	}
	Com_sprintf( line, sizeof( line ), "VKGAP %.3f |", gsum );
	for ( int k = 0; k < gn; k++ )
		if ( gms[k] >= 0.02 )
			Q_strcat( line, sizeof( line ), va( " Gap %s > %s: %.3fms", ga[k], gb[k], gms[k] ) );
	ri.Printf( PRINT_ALL, "%s\n", line );
}

// after vkBeginCommandBuffer: this slot's fence was waited, so its previous results are ready
void vk_gputime_begin_frame( void )
{
	active = qfalse;
	if ( !r_gpuTimes || !r_gpuTimes->integer )
		return;
	if ( !init_pools() )
		return;
	int slot = vk.cmd_index;
	print_results( slot );
	gpuFrameTimes_t *f = &frames[slot];
	f->numPasses = f->numQueries = 0;
	f->recorded = qfalse;
	pCmdResetQueryPool( vk.cmd->command_buffer, pools[slot], 0, GPUTIME_MAX_QUERIES );
	active = qtrue;
}

static void write_stamp( void )
{
	gpuFrameTimes_t *f = &frames[vk.cmd_index];
	pCmdWriteTimestamp( vk.cmd->command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pools[vk.cmd_index], f->numQueries++ );
	f->recorded = qtrue;
}

void vk_gputime_pass_begin( VkRenderPass handle )
{
	gpuFrameTimes_t *f = &frames[vk.cmd_index];
	const char *label = nextLabel;	// applies to this pass only, timed or not
	nextLabel = NULL;
	if ( !active || f->numQueries + 3 > GPUTIME_MAX_QUERIES )
		return;
	if ( f->numQueries & 1 )	// a pass is still open (should not happen): close it first
		return;
	f->name[f->numPasses] = label ? label : pass_name( handle );
	write_stamp();
}

// the next pass_begin is timed under this name (a static string) instead of its render pass's
void vk_gputime_next_label( const char *name )
{
	nextLabel = active ? name : NULL;
}

// inside an open pass: close it here and time the rest of the render pass under this name (a static string)
void vk_gputime_split( const char *name )
{
	gpuFrameTimes_t *f = &frames[vk.cmd_index];
	if ( !active || !( f->numQueries & 1 ) || f->numQueries + 3 > GPUTIME_MAX_QUERIES )
		return;
	write_stamp();
	f->numPasses++;
	f->name[f->numPasses] = name;
	write_stamp();
}

// outside any render pass: time the work up to vk_gputime_section_end (compute, copies) under this name (a static
// string); returns whether it opened one (not inside an open pass), to be passed to the end
qboolean vk_gputime_section_begin( const char *name )
{
	gpuFrameTimes_t *f = &frames[vk.cmd_index];
	if ( !active || ( f->numQueries & 1 ) )
		return qfalse;
	vk_gputime_next_label( name );
	vk_gputime_pass_begin( VK_NULL_HANDLE );
	return ( f->numQueries & 1 ) ? qtrue : qfalse;
}

void vk_gputime_section_end( qboolean opened )
{
	if ( opened )
		vk_gputime_pass_end();
}

void vk_gputime_pass_end( void )
{
	gpuFrameTimes_t *f = &frames[vk.cmd_index];
	if ( !active || !( f->numQueries & 1 ) )
		return;
	write_stamp();
	f->numPasses++;
}

/*
r_cpuTimes 1 prints one line per frame (bench; the clock is R_SunProfNow's steady clock):
	VKCPU <frame ms> | fe <ms> be <ms> fence <ms> acquire <ms> submit <ms> present <ms> latency <ms> other <ms>
frame = RE_EndFrame to RE_EndFrame (wall); fe = RE_RenderScene (the frontend, every scene of the frame); be = the
backend's command recording, its waits taken out: fence (vk_begin_frame waiting for the slot's last frame), acquire
(the swapchain image), submit, present, latency (r_maxFrameLatency's wait after present); other = the frame outside
the renderer (cgame, client, sound, events, the console and log, these prints).
*/
static double	cpuTimes[VK_CPUT_COUNT];
static double	cpuFrameStart;
static double	walkBuild[VK_WALK_COUNT], walkIssue[VK_WALK_COUNT];
static int		walkSurfs[VK_WALK_COUNT], walkCalls[VK_WALK_COUNT];
static double	secTimes[VK_SEC_COUNT];
static int		secCalls[VK_SEC_COUNT];
static const char *secNames[VK_SEC_COUNT] = { "2d", "drawsurfs", "drawbuffer", "swap", "worldfx", "othercmd", "cascades",
	"beginview", "charocc", "sunflares", "dlights", "resolve", "uniforms", "g2const", "g2xform", "beginframe", "cloth", "scenebegin",
	"scenestream", "scenefaces", "scenecull", "texstream", "scenepre", "scenerotate", "scenecullrec", "scenepack", "scenedraw",
	"scenehaze", "scenedecals", "sceneocc", "scenecapture", "scenegrade" };

void vk_cputime_section( int slot, double ms )
{
	if ( slot < 0 || slot >= VK_SEC_COUNT )
		return;
	secTimes[slot] += ms;
	secCalls[slot]++;
}

static const char *walkNames[VK_WALK_COUNT] = { "lit", "prepass", "cascade", "chartile", "hairmap", "charocc", "glow",
	"refraction", "post1x", "other" };

// VKWALK <name> <build ms> <issue ms> <surfaces> <walks> ...: RB_SubmitDrawSurfs (per-surface work, tessellation) and
// RB_SubmitRenderPass (the Vulkan commands) of every draw-surface walk this frame, summed per kind
void vk_cputime_walk( int slot, double buildMs, double issueMs, int surfs )
{
	if ( slot < 0 || slot >= VK_WALK_COUNT )
		return;
	walkBuild[slot] += buildMs;
	walkIssue[slot] += issueMs;
	walkSurfs[slot] += surfs;
	walkCalls[slot]++;
}

qboolean vk_cputime_active( void )
{
	return ( r_cpuTimes && r_cpuTimes->integer ) ? qtrue : qfalse;
}

void vk_cputime_add( int slot, double ms )
{
	if ( slot >= 0 && slot < VK_CPUT_COUNT )
		cpuTimes[slot] += ms;
}

void vk_cputime_end_frame( void )
{
	const double now = R_SunProfNow();
	if ( vk_cputime_active() && cpuFrameStart > 0.0 ) {
		const double *t = cpuTimes;
		const double waits = t[VK_CPUT_FENCE] + t[VK_CPUT_ACQUIRE] + t[VK_CPUT_SUBMIT] + t[VK_CPUT_PRESENT] + t[VK_CPUT_LATENCY];
		const double frame = now - cpuFrameStart;
		ri.Printf( PRINT_ALL, "VKCPU %.3f | fe %.3f be %.3f fence %.3f acquire %.3f submit %.3f present %.3f latency %.3f other %.3f\n",
			frame, t[VK_CPUT_FE], t[VK_CPUT_BE] - waits, t[VK_CPUT_FENCE], t[VK_CPUT_ACQUIRE], t[VK_CPUT_SUBMIT],
			t[VK_CPUT_PRESENT], t[VK_CPUT_LATENCY], frame - t[VK_CPUT_FE] - t[VK_CPUT_BE] );
		char line[1024];
		Q_strncpyz( line, "VKWALK", sizeof( line ) );
		for ( int i = 0; i < VK_WALK_COUNT; i++ )
			if ( walkCalls[i] )
				Q_strcat( line, sizeof( line ), va( " %s %.3f %.3f %d %d", walkNames[i], walkBuild[i], walkIssue[i], walkSurfs[i], walkCalls[i] ) );
		ri.Printf( PRINT_ALL, "%s\n", line );
		Q_strncpyz( line, "VKSEC", sizeof( line ) );
		for ( int i = 0; i < VK_SEC_COUNT; i++ )
			if ( secCalls[i] )
				Q_strcat( line, sizeof( line ), va( " %s %.3f %d", secNames[i], secTimes[i], secCalls[i] ) );
		ri.Printf( PRINT_ALL, "%s\n", line );
	}
	Com_Memset( secTimes, 0, sizeof( secTimes ) );
	Com_Memset( secCalls, 0, sizeof( secCalls ) );
	Com_Memset( cpuTimes, 0, sizeof( cpuTimes ) );
	Com_Memset( walkBuild, 0, sizeof( walkBuild ) );
	Com_Memset( walkIssue, 0, sizeof( walkIssue ) );
	Com_Memset( walkSurfs, 0, sizeof( walkSurfs ) );
	Com_Memset( walkCalls, 0, sizeof( walkCalls ) );
	cpuFrameStart = now;
}

void vk_gputime_shutdown( void )
{
	for ( int i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		if ( pools[i] && pDestroyQueryPool )
			pDestroyQueryPool( vk.device, pools[i], NULL );
		pools[i] = VK_NULL_HANDLE;
	}
	numPassNames = 0;
	active = qfalse;
}
