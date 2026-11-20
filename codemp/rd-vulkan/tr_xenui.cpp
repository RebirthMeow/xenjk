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
xenui's renderer interface (rd-common/xenui_render.h, docs/wishlist/ui-system.md section 4): RmlUi's geometry, textures,
scissor and transform, and its optional features: clip masks, layers, filters and gradient shaders.

Every call is a render command, so it runs on the backend in the order RmlUi made it. The backend records xenui's
drawing into a command buffer of its own (one per frame slot), into targets of its own at the window's size: a layer
stack (multisampled as the main pass, resolved into a sampled image), one stencil shared by the layers (the clip mask)
and scratch images for filters. R_XenUISubmit submits it ahead of the frame (vk_end_frame), as vk_upload.cpp does,
ending in a barrier whose second scope covers the frame. RC_XENUI_END then draws the base layer over the frame through
the 2D path, so the main pass's own variants (multisample, bloom) need nothing new.

A frame takes up to XENUI_MAX_PASSES document passes, each ended by EndFrame and drawn where its RC_XENUI_END falls in
the frame's commands: a screen's underlay (drawn under its 3D stage) and the overlay. Each pass has a base layer of its
own; the layers above the base, the stencil and the scratch images are shared.

Compiled geometry stays in renderer memory and is copied into the slot's stream buffer at each draw.
*/
#include "tr_local.h"
#include "rd-common/xenui_render.h"

#include <vector>

extern "C" {
extern const unsigned char xenui_geom_vert_spv[];
extern const unsigned int xenui_geom_vert_spv_size;
extern const unsigned char xenui_quad_vert_spv[];
extern const unsigned int xenui_quad_vert_spv_size;
extern const unsigned char xenui_color_frag_spv[];
extern const unsigned int xenui_color_frag_spv_size;
extern const unsigned char xenui_texture_frag_spv[];
extern const unsigned int xenui_texture_frag_spv_size;
extern const unsigned char xenui_gradient_frag_spv[];
extern const unsigned int xenui_gradient_frag_spv_size;
extern const unsigned char xenui_post_frag_spv[];
extern const unsigned int xenui_post_frag_spv_size;
}

#define XENUI_MAX_GEOMETRY	16384
#define XENUI_MAX_TEXTURES	4096
#define XENUI_MAX_FILTERS	1024
#define XENUI_MAX_SHADERS	1024
#define XENUI_MAX_LAYERS	16
#define XENUI_NUM_POST		3
#define XENUI_STREAM_SIZE	( 8u << 20 )	// per frame slot: vertices, indexes and gradient blocks
#define XENUI_GRADIENT_RANGE	512u		// the gradient block's bound range, reserved whole in the stream
#define XENUI_MAX_COMPOSITE_FILTERS	8
#define XENUI_MAX_PASSES	2		// documents a frame (EndFrame each)

// --- frontend: handles and the command stream ------------------------------------------------------------------------

typedef struct {
	xenUIVertex_t	*verts;
	int				*indexes;
	int				numVerts;
	int				numIndexes;
} xenUIGeometry_t;

typedef struct {
	qboolean		used;
	image_t			*image;			// a loaded or generated texture (the engine's image, never freed)
	VkImage			own;			// a saved layer (SaveLayerAsTexture): xenui's own image
	VkDeviceMemory	ownMemory;
	VkImageView		ownView;
	VkDescriptorSet	set;
	int				width, height;
	int				freeAt;			// a released own image: freed once this frame's slot comes round again
	int				releasedAt;		// the frontend frame it was released in (XenUI_Reusable)
} xenUITexture_t;

typedef struct {
	qboolean		used;
	qboolean		mask;			// SaveLayerAsMaskImage: the mask snapshot
	int				releasedAt;
	xenUIFilter_t	f;
} xenUIFilterSlot_t;

typedef struct {
	qboolean		used;
	int				releasedAt;
	xenUIShader_t	s;
} xenUIShaderSlot_t;

enum {
	XOP_GEOMETRY, XOP_RELEASE_GEOMETRY, XOP_SCISSOR, XOP_TRANSFORM, XOP_CLIP_ENABLE, XOP_CLIP_RENDER, XOP_PUSH,
	XOP_POP, XOP_COMPOSITE, XOP_SAVE_TEXTURE, XOP_SAVE_MASK, XOP_SHADER, XOP_RELEASE_TEXTURE, XOP_END
};

typedef struct {
	int		commandId;	// RC_XENUI
	int		op;
	int		a, b, c, d, e;
	int		filters[XENUI_MAX_COMPOSITE_FILTERS];
	float	tx, ty;
	float	m[16];
} xenUICommand_t;

static xenUIGeometry_t		xenGeometry[XENUI_MAX_GEOMETRY];
static xenUITexture_t		xenTextures[XENUI_MAX_TEXTURES];
static xenUIFilterSlot_t	xenFilters[XENUI_MAX_FILTERS];
static xenUIShaderSlot_t	xenShaders[XENUI_MAX_SHADERS];

static struct {
	int		layers;			// layers pushed above the base
	int		refused;		// pushes refused (layer limit, full command buffer): their pops are swallowed
	int		scissor, scissorRect[4];
	int		drawn;			// commands sent this frame (EndFrame sends nothing otherwise)
} xf;

// A released handle is read by the backend when it runs this frame's commands, after the frontend has moved on, and
// RmlUi releases a filter right after the composite that uses it: a slot is reused two frames after its release
// (renderer frames: a frame can end two passes).
static qboolean XenUI_Reusable( qboolean used, int releasedAt )
{
	return (qboolean)( !used && ( !releasedAt || tr.frameCount >= releasedAt + 2 ) );
}

static xenUICommand_t *XenUI_Command( int op )
{
	xenUICommand_t *cmd;

	if ( !tr.registered )
		return NULL;
	cmd = (xenUICommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd )
		return NULL;
	memset( cmd, 0, sizeof( *cmd ) );
	cmd->commandId = RC_XENUI;
	cmd->op = op;
	xf.drawn = 1;
	return cmd;
}

// --- backend state ---------------------------------------------------------------------------------------------------

typedef struct {
	VkImage			image;
	VkDeviceMemory	memory;
	VkImageView		view;
} xenUIImage_t;

typedef struct {
	xenUIImage_t	msaa;			// samples > 1: the attachment drawn into
	xenUIImage_t	color;			// sampled; the resolve target (or the attachment itself at one sample)
	VkDescriptorSet	set;
	VkFramebuffer	framebuffer;
} xenUITarget_t;

typedef struct {
	VkCommandBuffer	cmd;
	VkFence			fence;
	qboolean		submitted;		// the fence will signal
	VkBuffer		stream;
	VkDeviceMemory	streamMemory;
	byte			*streamPtr;
	uint32_t		streamUsed;
	VkDescriptorSet	ubo;			// the stream as a dynamic uniform buffer (gradients)
} xenUISlot_t;

enum { XP_COLOR, XP_TEXTURE, XP_GRADIENT };

static struct {
	qboolean				ready, failed;
	int						width, height;
	VkSampleCountFlagBits	samples;
	VkFormat				stencilFormat;
	VkCommandPool			pool;
	VkDescriptorPool		descriptorPool;
	VkDescriptorSetLayout	uboLayout;
	VkSampler				sampler;
	VkRenderPass			rpLayer, rpLayerClear, rpPost;
	VkPipelineLayout		geomLayout, postLayout;
	VkShaderModule			geomVert, quadVert, colorFrag, textureFrag, gradientFrag, postFrag;
	VkPipeline				geom[3][2];			// [XP_*][stencil test]
	VkPipeline				clipReplace, clipIncrement;
	VkPipeline				composite[2][2];	// [blend mode][stencil test]
	VkPipeline				post, postBlend;
	PFN_vkCmdSetStencilReference	setStencilReference;
	// GPU time of the slot's command buffer (timestamps at its start and end), read when the slot comes round
	VkQueryPool				queries;
	PFN_vkCmdResetQueryPool	resetQueryPool;
	PFN_vkCmdWriteTimestamp	writeTimestamp;
	PFN_vkGetQueryPoolResults	queryResults;
	float					nsPerTick;
	qboolean				timed[NUM_COMMAND_BUFFERS];
	float					gpuMs;				// the last frame read
	xenUIImage_t			stencil;
	xenUITarget_t			layers[XENUI_MAX_LAYERS];
	int						numLayers;			// layer targets made so far
	xenUITarget_t			postTargets[XENUI_NUM_POST];
	xenUITarget_t			maskTarget;
	VkDescriptorSet			whiteSet;			// untextured draws still bind set 0
	image_t					outImage[XENUI_MAX_PASSES];	// each pass's base layer, resolved: drawn at its RC_XENUI_END
	shader_t				*outShader[XENUI_MAX_PASSES];
	xenUITarget_t			bases[XENUI_MAX_PASSES];	// the base targets not in layers[0] now (made at first use)
	int						baseIn0;			// the pass whose base is in layers[0]
	xenUISlot_t				slots[NUM_COMMAND_BUFFERS];
	int						frame;				// frames recorded (textures released at frame N free at N + slots)

	// recording
	qboolean				recording;
	int						recFrame;			// tr.frameCount of the frame recording
	int						pass;				// passes ended this frame
	qboolean				passOpen;			// a pass has begun (its base cleared)
	int						slot;
	int						open;				// the layer whose render pass is open, or -1
	int						top;
	int						refused;			// pushes that made no layer: their pops are swallowed
	qboolean				scissor;
	VkRect2D				scissorRect;
	qboolean				transform;
	float					m[16];
	qboolean				clip;
	uint32_t				stencilRef;
	VkPipeline				bound;
} xb;

// --- frontend API ----------------------------------------------------------------------------------------------------

static void XenUI_FreeGeometry( int geometry )
{
	xenUIGeometry_t *g = &xenGeometry[geometry];

	if ( g->verts ) {
		ri.Z_Free( g->verts );
		ri.Z_Free( g->indexes );
	}
	memset( g, 0, sizeof( *g ) );
}

static int XenUI_CompileGeometry( const xenUIVertex_t *verts, int numVerts, const int *indexes, int numIndexes )
{
	int i;

	if ( numVerts <= 0 || numIndexes <= 0 )
		return 0;

	for ( i = 1; i < XENUI_MAX_GEOMETRY; i++ ) {
		xenUIGeometry_t *g = &xenGeometry[i];

		if ( g->verts )
			continue;
		g->verts = (xenUIVertex_t *)ri.Z_Malloc( numVerts * sizeof( xenUIVertex_t ), TAG_GENERAL, qfalse, 4 );
		g->indexes = (int *)ri.Z_Malloc( numIndexes * sizeof( int ), TAG_GENERAL, qfalse, 4 );
		memcpy( g->verts, verts, numVerts * sizeof( xenUIVertex_t ) );
		memcpy( g->indexes, indexes, numIndexes * sizeof( int ) );
		g->numVerts = numVerts;
		g->numIndexes = numIndexes;
		return i;
	}

	ri.Printf( PRINT_WARNING, "xenui: more than %d compiled geometries\n", XENUI_MAX_GEOMETRY - 1 );
	return 0;
}

static void XenUI_ReleaseGeometry( int geometry )
{
	xenUICommand_t *cmd;

	if ( geometry <= 0 || geometry >= XENUI_MAX_GEOMETRY )
		return;
	cmd = XenUI_Command( XOP_RELEASE_GEOMETRY );
	if ( !cmd ) {	// no frame to order against
		XenUI_FreeGeometry( geometry );
		return;
	}
	cmd->a = geometry;
}

static void XenUI_RenderGeometry( int geometry, float tx, float ty, int texture )
{
	xenUICommand_t *cmd;

	if ( geometry <= 0 || geometry >= XENUI_MAX_GEOMETRY || !xenGeometry[geometry].verts )
		return;
	cmd = XenUI_Command( XOP_GEOMETRY );
	if ( !cmd )
		return;
	cmd->a = geometry;
	cmd->b = ( texture > 0 && texture < XENUI_MAX_TEXTURES && xenTextures[texture].used ) ? texture : 0;
	cmd->tx = tx;
	cmd->ty = ty;
}

static int XenUI_NewTexture( void )
{
	for ( int i = 1; i < XENUI_MAX_TEXTURES; i++ ) {
		if ( XenUI_Reusable( xenTextures[i].used, xenTextures[i].releasedAt ) && !xenTextures[i].own ) {
			memset( &xenTextures[i], 0, sizeof( xenTextures[i] ) );
			xenTextures[i].used = qtrue;
			return i;
		}
	}
	ri.Printf( PRINT_WARNING, "xenui: more than %d textures\n", XENUI_MAX_TEXTURES - 1 );
	return 0;
}

static int XenUI_TextureFromPixels( byte *rgba, int width, int height )
{
	static int	serial;
	image_t		*image;
	int			t = XenUI_NewTexture();

	if ( !t )
		return 0;
	image = R_CreateImage( va( "*xenui%d", ++serial ), rgba, width, height,
		(imgFlags_t)( IMGFLAG_CLAMPTOEDGE | IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_NOSCALE ), 0, 0 );
	if ( !image ) {
		xenTextures[t].used = qfalse;
		return 0;
	}
	xenTextures[t].image = image;
	xenTextures[t].set = image->descriptor_set;
	xenTextures[t].width = width;
	xenTextures[t].height = height;
	return t;
}

static int XenUI_CreateTexture( const unsigned char *rgba, int width, int height )
{
	if ( !tr.registered || !rgba || width <= 0 || height <= 0 )
		return 0;
	return XenUI_TextureFromPixels( (byte *)rgba, width, height );
}

static int XenUI_LoadTexture( const char *path, int *width, int *height )
{
	byte	*pic;
	int		w, h, i, texture;

	*width = *height = 0;
	if ( !tr.registered || !path || !path[0] )
		return 0;

	R_LoadImage( path, &pic, &w, &h );
	if ( !pic )
		return 0;

	for ( i = 0; i < w * h; i++ ) {	// RmlUi draws premultiplied
		byte *p = pic + i * 4;
		p[0] = (byte)( ( p[0] * p[3] + 127 ) / 255 );
		p[1] = (byte)( ( p[1] * p[3] + 127 ) / 255 );
		p[2] = (byte)( ( p[2] * p[3] + 127 ) / 255 );
	}

	texture = XenUI_TextureFromPixels( pic, w, h );
	ri.Z_Free( pic );
	if ( texture ) {
		*width = w;
		*height = h;
	}
	return texture;
}

static void XenUI_ReleaseTexture( int texture )
{
	xenUICommand_t *cmd;

	if ( texture <= 0 || texture >= XENUI_MAX_TEXTURES || !xenTextures[texture].used )
		return;
	xenTextures[texture].releasedAt = tr.frameCount + 1;
	if ( !xenTextures[texture].own ) {	// the engine's images live until the renderer shuts down
		xenTextures[texture].used = qfalse;
		return;
	}
	cmd = XenUI_Command( XOP_RELEASE_TEXTURE );
	if ( cmd )
		cmd->a = texture;
	else	// no command to carry the release: freed as the backend would have (XenUI_Begin)
		xenTextures[texture].freeAt = xb.frame + NUM_COMMAND_BUFFERS + 1;
	xenTextures[texture].used = qfalse;	// the handle is not reused until the backend frees the image
}

static void XenUI_SetScissor( int enable, int x, int y, int width, int height )
{
	xenUICommand_t *cmd = XenUI_Command( XOP_SCISSOR );

	xf.scissor = enable;
	xf.scissorRect[0] = x;
	xf.scissorRect[1] = y;
	xf.scissorRect[2] = width;
	xf.scissorRect[3] = height;
	if ( cmd ) {
		cmd->a = enable;
		cmd->b = x;
		cmd->c = y;
		cmd->d = width;
		cmd->e = height;
	}
}

static void XenUI_SetTransform( const float *matrix )
{
	xenUICommand_t *cmd = XenUI_Command( XOP_TRANSFORM );

	if ( cmd ) {
		cmd->a = matrix != NULL;
		if ( matrix )
			memcpy( cmd->m, matrix, sizeof( cmd->m ) );
	}
}

static void XenUI_GetWindowSize( int *width, int *height )
{
	*width = glConfig.vidWidth;
	*height = glConfig.vidHeight;
}

static void XenUI_EnableClipMask( int enable )
{
	xenUICommand_t *cmd = XenUI_Command( XOP_CLIP_ENABLE );

	if ( cmd )
		cmd->a = enable;
}

static void XenUI_RenderToClipMask( int operation, int geometry, float tx, float ty )
{
	xenUICommand_t *cmd;

	if ( geometry <= 0 || geometry >= XENUI_MAX_GEOMETRY || !xenGeometry[geometry].verts )
		return;
	cmd = XenUI_Command( XOP_CLIP_RENDER );
	if ( cmd ) {
		cmd->a = operation;
		cmd->b = geometry;
		cmd->tx = tx;
		cmd->ty = ty;
	}
}

static int XenUI_PushLayer( void )
{
	if ( xf.layers + 1 >= XENUI_MAX_LAYERS ) {
		ri.Printf( PRINT_WARNING, "xenui: more than %d layers\n", XENUI_MAX_LAYERS - 1 );
		xf.refused++;
		return xf.layers;
	}
	if ( !XenUI_Command( XOP_PUSH ) ) {
		xf.refused++;
		return xf.layers;
	}
	return ++xf.layers;
}

static void XenUI_CompositeLayers( int source, int destination, int blendMode, const int *filters, int numFilters )
{
	xenUICommand_t *cmd = XenUI_Command( XOP_COMPOSITE );
	int i;

	if ( !cmd )
		return;
	cmd->a = source;
	cmd->b = destination;
	cmd->c = blendMode;
	for ( i = 0; i < numFilters && cmd->d < XENUI_MAX_COMPOSITE_FILTERS; i++ ) {
		if ( filters[i] > 0 && filters[i] < XENUI_MAX_FILTERS && xenFilters[filters[i]].used )
			cmd->filters[cmd->d++] = filters[i];
	}
}

static void XenUI_PopLayer( void )
{
	if ( xf.refused > 0 ) {
		xf.refused--;
		return;
	}
	if ( xf.layers > 0 ) {
		XenUI_Command( XOP_POP );
		xf.layers--;
	}
}

static int XenUI_SaveLayerAsTexture( void )
{
	xenUICommand_t	*cmd;
	int				t, w, h;

	w = xf.scissor ? xf.scissorRect[2] : glConfig.vidWidth;
	h = xf.scissor ? xf.scissorRect[3] : glConfig.vidHeight;
	if ( w <= 0 || h <= 0 )
		return 0;
	t = XenUI_NewTexture();
	if ( !t )
		return 0;
	cmd = XenUI_Command( XOP_SAVE_TEXTURE );
	if ( !cmd ) {
		xenTextures[t].used = qfalse;
		return 0;
	}
	xenTextures[t].width = w;
	xenTextures[t].height = h;
	cmd->a = t;
	return t;
}

static int XenUI_NewFilter( void )
{
	for ( int i = 1; i < XENUI_MAX_FILTERS; i++ ) {
		if ( XenUI_Reusable( xenFilters[i].used, xenFilters[i].releasedAt ) ) {
			memset( &xenFilters[i], 0, sizeof( xenFilters[i] ) );
			xenFilters[i].used = qtrue;
			return i;
		}
	}
	ri.Printf( PRINT_WARNING, "xenui: more than %d filters\n", XENUI_MAX_FILTERS - 1 );
	return 0;
}

static int XenUI_SaveLayerAsMaskImage( void )
{
	const int f = XenUI_NewFilter();

	if ( f ) {
		xenFilters[f].mask = qtrue;
		XenUI_Command( XOP_SAVE_MASK );
	}
	return f;
}

static int XenUI_CompileFilter( const xenUIFilter_t *filter )
{
	const int f = filter ? XenUI_NewFilter() : 0;

	if ( f )
		xenFilters[f].f = *filter;
	return f;
}

static void XenUI_ReleaseFilter( int filter )
{
	if ( filter > 0 && filter < XENUI_MAX_FILTERS && xenFilters[filter].used ) {	// no GPU objects to free
		xenFilters[filter].used = qfalse;
		xenFilters[filter].releasedAt = tr.frameCount + 1;
	}
}

static int XenUI_CompileShader( const xenUIShader_t *shader )
{
	if ( !shader )
		return 0;
	for ( int i = 1; i < XENUI_MAX_SHADERS; i++ ) {
		if ( XenUI_Reusable( xenShaders[i].used, xenShaders[i].releasedAt ) ) {
			xenShaders[i].used = qtrue;
			xenShaders[i].releasedAt = 0;
			xenShaders[i].s = *shader;
			return i;
		}
	}
	ri.Printf( PRINT_WARNING, "xenui: more than %d shaders\n", XENUI_MAX_SHADERS - 1 );
	return 0;
}

static void XenUI_RenderShader( int shader, int geometry, float tx, float ty, int texture )
{
	xenUICommand_t *cmd;

	if ( shader <= 0 || shader >= XENUI_MAX_SHADERS || !xenShaders[shader].used )
		return;
	if ( geometry <= 0 || geometry >= XENUI_MAX_GEOMETRY || !xenGeometry[geometry].verts )
		return;
	cmd = XenUI_Command( XOP_SHADER );
	if ( cmd ) {
		cmd->a = geometry;
		cmd->b = shader;
		cmd->tx = tx;
		cmd->ty = ty;
	}
}

static void XenUI_ReleaseShader( int shader )
{
	if ( shader > 0 && shader < XENUI_MAX_SHADERS && xenShaders[shader].used ) {
		xenShaders[shader].used = qfalse;
		xenShaders[shader].releasedAt = tr.frameCount + 1;
	}
}

static void XenUI_EndFrame( void )
{
	if ( xf.drawn )
		XenUI_Command( XOP_END );
	xf.drawn = 0;
	xf.layers = 0;
	xf.refused = 0;
}

static float XenUI_GPUTime( void )
{
	return xb.gpuMs;
}

static const xenUIRenderAPI_t xenUIRenderAPI = {
	XENUI_RENDER_API_VERSION,
	sizeof( xenUIRenderAPI_t ),
	XenUI_CompileGeometry,
	XenUI_ReleaseGeometry,
	XenUI_RenderGeometry,
	XenUI_CreateTexture,
	XenUI_LoadTexture,
	XenUI_ReleaseTexture,
	XenUI_SetScissor,
	XenUI_SetTransform,
	XenUI_GetWindowSize,
	XenUI_EnableClipMask,
	XenUI_RenderToClipMask,
	XenUI_PushLayer,
	XenUI_CompositeLayers,
	XenUI_PopLayer,
	XenUI_SaveLayerAsTexture,
	XenUI_SaveLayerAsMaskImage,
	XenUI_CompileFilter,
	XenUI_ReleaseFilter,
	XenUI_CompileShader,
	XenUI_RenderShader,
	XenUI_ReleaseShader,
	XenUI_EndFrame,
	XenUI_GPUTime,
};

extern "C" Q_EXPORT const xenUIRenderAPI_t * QDECL XenUI_GetRenderAPI( int version )
{
	return version >= 1 && version <= XENUI_RENDER_API_VERSION ? &xenUIRenderAPI : NULL;
}

// --- backend: Vulkan objects -----------------------------------------------------------------------------------------

static qboolean XenUI_Image( xenUIImage_t *img, VkFormat format, VkSampleCountFlagBits samples, VkImageUsageFlags usage,
	VkImageAspectFlags aspect, int width, int height )
{
	VkImageCreateInfo		ic;
	VkMemoryRequirements	req;
	VkMemoryAllocateInfo	ai;
	VkImageViewCreateInfo	vc;

	memset( &ic, 0, sizeof( ic ) );
	ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	ic.imageType = VK_IMAGE_TYPE_2D;
	ic.format = format;
	ic.extent.width = width;
	ic.extent.height = height;
	ic.extent.depth = 1;
	ic.mipLevels = 1;
	ic.arrayLayers = 1;
	ic.samples = samples;
	ic.tiling = VK_IMAGE_TILING_OPTIMAL;
	ic.usage = usage;
	ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( qvkCreateImage( vk.device, &ic, NULL, &img->image ) != VK_SUCCESS )
		return qfalse;

	qvkGetImageMemoryRequirements( vk.device, img->image, &req );
	memset( &ai, 0, sizeof( ai ) );
	ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( vk_vram_allocate_named( vk.device, &ai, &img->memory, "xenui" ) != VK_SUCCESS )
		return qfalse;
	VK_CHECK( qvkBindImageMemory( vk.device, img->image, img->memory, 0 ) );

	memset( &vc, 0, sizeof( vc ) );
	vc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	vc.image = img->image;
	vc.viewType = VK_IMAGE_VIEW_TYPE_2D;
	vc.format = format;
	vc.subresourceRange.aspectMask = aspect;
	vc.subresourceRange.levelCount = 1;
	vc.subresourceRange.layerCount = 1;
	return qvkCreateImageView( vk.device, &vc, NULL, &img->view ) == VK_SUCCESS ? qtrue : qfalse;
}

static void XenUI_DestroyImage( xenUIImage_t *img )
{
	if ( img->view )
		qvkDestroyImageView( vk.device, img->view, NULL );
	if ( img->image )
		qvkDestroyImage( vk.device, img->image, NULL );
	if ( img->memory )
		qvkFreeMemory( vk.device, img->memory, NULL );
	memset( img, 0, sizeof( *img ) );
}

static VkDescriptorSet XenUI_SamplerSet( VkImageView view )
{
	VkDescriptorSetAllocateInfo	ai;
	VkDescriptorImageInfo		ii;
	VkWriteDescriptorSet		w;
	VkDescriptorSet				set = VK_NULL_HANDLE;

	memset( &ai, 0, sizeof( ai ) );
	ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	ai.descriptorPool = xb.descriptorPool;
	ai.descriptorSetCount = 1;
	ai.pSetLayouts = &vk.set_layout_sampler;
	if ( qvkAllocateDescriptorSets( vk.device, &ai, &set ) != VK_SUCCESS )
		return VK_NULL_HANDLE;

	ii.sampler = xb.sampler;
	ii.imageView = view;
	ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	memset( &w, 0, sizeof( w ) );
	w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	w.dstSet = set;
	w.descriptorCount = 1;
	w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	w.pImageInfo = &ii;
	qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	return set;
}

static void XenUI_Barrier( VkImage image, VkImageAspectFlags aspect, VkImageLayout from, VkImageLayout to,
	VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess )
{
	VkImageMemoryBarrier b;

	memset( &b, 0, sizeof( b ) );
	b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	b.srcAccessMask = srcAccess;
	b.dstAccessMask = dstAccess;
	b.oldLayout = from;
	b.newLayout = to;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.image = image;
	b.subresourceRange.aspectMask = aspect;
	b.subresourceRange.levelCount = 1;
	b.subresourceRange.layerCount = 1;
	qvkCmdPipelineBarrier( xb.slots[xb.slot].cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b );
}

// a layer's attachment images; the sampled image rests in SHADER_READ_ONLY, the multisampled one in COLOR_ATTACHMENT
static qboolean XenUI_Target( xenUITarget_t *t, qboolean layer )
{
	const VkFormat				format = VK_FORMAT_R8G8B8A8_UNORM;
	const qboolean				msaa = (qboolean)( layer && xb.samples != VK_SAMPLE_COUNT_1_BIT );
	VkImageView					views[3];
	VkFramebufferCreateInfo		fc;
	uint32_t					n = 0;

	if ( !XenUI_Image( &t->color, format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, xb.width, xb.height ) )
		return qfalse;
	if ( msaa && !XenUI_Image( &t->msaa, format, xb.samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		VK_IMAGE_ASPECT_COLOR_BIT, xb.width, xb.height ) )
		return qfalse;
	t->set = XenUI_SamplerSet( t->color.view );
	if ( !t->set )
		return qfalse;

	// layer: [color or msaa, stencil, resolve]; post: [color]
	views[n++] = msaa ? t->msaa.view : t->color.view;
	if ( layer ) {
		views[n++] = xb.stencil.view;
		if ( msaa )
			views[n++] = t->color.view;
	}
	memset( &fc, 0, sizeof( fc ) );
	fc.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fc.renderPass = layer ? xb.rpLayer : xb.rpPost;
	fc.attachmentCount = n;
	fc.pAttachments = views;
	fc.width = xb.width;
	fc.height = xb.height;
	fc.layers = 1;
	if ( qvkCreateFramebuffer( vk.device, &fc, NULL, &t->framebuffer ) != VK_SUCCESS )
		return qfalse;

	XenUI_Barrier( t->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );
	if ( msaa ) {
		XenUI_Barrier( t->msaa.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT );
	}
	return qtrue;
}

static void XenUI_DestroyTarget( xenUITarget_t *t )
{
	if ( t->framebuffer )
		qvkDestroyFramebuffer( vk.device, t->framebuffer, NULL );
	if ( t->set )
		qvkFreeDescriptorSets( vk.device, xb.descriptorPool, 1, &t->set );
	XenUI_DestroyImage( &t->msaa );
	XenUI_DestroyImage( &t->color );
	memset( t, 0, sizeof( *t ) );
}

static VkRenderPass XenUI_RenderPass( qboolean layer, qboolean clear )
{
	const qboolean				msaa = (qboolean)( layer && xb.samples != VK_SAMPLE_COUNT_1_BIT );
	const VkImageLayout			sampled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkAttachmentDescription		att[3];
	VkAttachmentReference		colorRef, stencilRef, resolveRef;
	VkSubpassDescription		sub;
	VkSubpassDependency			dep[2];
	VkRenderPassCreateInfo		rc;
	VkRenderPass				rp = VK_NULL_HANDLE;
	uint32_t					n = 0;

	memset( att, 0, sizeof( att ) );
	// the drawn colour: the multisampled image (rests as an attachment) or the sampled image itself
	att[n].format = VK_FORMAT_R8G8B8A8_UNORM;
	att[n].samples = layer ? xb.samples : VK_SAMPLE_COUNT_1_BIT;
	att[n].loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	att[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	att[n].initialLayout = msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : sampled;
	att[n].finalLayout = att[n].initialLayout;
	n++;
	if ( layer ) {
		att[n].format = xb.stencilFormat;
		att[n].samples = xb.samples;
		att[n].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		att[n].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		att[n].stencilLoadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
		att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
		att[n].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		att[n].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		n++;
		if ( msaa ) {	// written whole by the resolve at the end of the pass
			att[n].format = VK_FORMAT_R8G8B8A8_UNORM;
			att[n].samples = VK_SAMPLE_COUNT_1_BIT;
			att[n].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[n].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			att[n].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att[n].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att[n].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			att[n].finalLayout = sampled;
			n++;
		}
	}

	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	stencilRef.attachment = 1;
	stencilRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	resolveRef.attachment = 2;
	resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	memset( &sub, 0, sizeof( sub ) );
	sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	sub.colorAttachmentCount = 1;
	sub.pColorAttachments = &colorRef;
	sub.pResolveAttachments = msaa ? &resolveRef : NULL;
	sub.pDepthStencilAttachment = layer ? &stencilRef : NULL;

	// earlier passes' writes and samples before this pass; this pass's writes before later samples and copies
	memset( dep, 0, sizeof( dep ) );
	dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dep[0].dstSubpass = 0;
	dep[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
	dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dep[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_TRANSFER_WRITE_BIT;
	dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
	dep[1].srcSubpass = 0;
	dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

	memset( &rc, 0, sizeof( rc ) );
	rc.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	rc.attachmentCount = n;
	rc.pAttachments = att;
	rc.subpassCount = 1;
	rc.pSubpasses = &sub;
	rc.dependencyCount = 2;
	rc.pDependencies = dep;
	VK_CHECK( qvkCreateRenderPass( vk.device, &rc, NULL, &rp ) );
	return rp;
}

static VkShaderModule XenUI_Shader( const unsigned char *code, unsigned int size )
{
	VkShaderModuleCreateInfo	sc;
	VkShaderModule				m = VK_NULL_HANDLE;

	memset( &sc, 0, sizeof( sc ) );
	sc.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	sc.codeSize = size;
	sc.pCode = (const uint32_t *)code;
	VK_CHECK( qvkCreateShaderModule( vk.device, &sc, NULL, &m ) );
	return m;
}

enum { XS_OFF, XS_TEST, XS_CLIP_REPLACE, XS_CLIP_INCREMENT };

static VkPipeline XenUI_Pipeline( qboolean geometry, VkShaderModule frag, VkPipelineLayout layout, VkRenderPass rp,
	qboolean layer, qboolean blend, int stencil )
{
	VkPipelineShaderStageCreateInfo			stages[2];
	VkVertexInputBindingDescription			binding;
	VkVertexInputAttributeDescription		attrs[3];
	VkPipelineVertexInputStateCreateInfo	vi;
	VkPipelineInputAssemblyStateCreateInfo	ia;
	VkPipelineViewportStateCreateInfo		vp;
	VkPipelineRasterizationStateCreateInfo	rs;
	VkPipelineMultisampleStateCreateInfo	ms;
	VkPipelineDepthStencilStateCreateInfo	ds;
	VkPipelineColorBlendAttachmentState		cba;
	VkPipelineColorBlendStateCreateInfo		cb;
	VkDynamicState							dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE };
	VkPipelineDynamicStateCreateInfo		dc;
	VkGraphicsPipelineCreateInfo			pc;
	VkPipeline								p = VK_NULL_HANDLE;

	memset( stages, 0, sizeof( stages ) );
	stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = geometry ? xb.geomVert : xb.quadVert;
	stages[0].pName = "main";
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = frag;
	stages[1].pName = "main";

	binding.binding = 0;
	binding.stride = sizeof( xenUIVertex_t );
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
	attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R8G8B8A8_UNORM; attrs[1].offset = 8;
	attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = 12;
	memset( &vi, 0, sizeof( vi ) );
	vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if ( geometry ) {
		vi.vertexBindingDescriptionCount = 1;
		vi.pVertexBindingDescriptions = &binding;
		vi.vertexAttributeDescriptionCount = 3;
		vi.pVertexAttributeDescriptions = attrs;
	}

	memset( &ia, 0, sizeof( ia ) );
	ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	memset( &vp, 0, sizeof( vp ) );
	vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	vp.viewportCount = 1;
	vp.scissorCount = 1;

	memset( &rs, 0, sizeof( rs ) );
	rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rs.polygonMode = VK_POLYGON_MODE_FILL;
	rs.cullMode = VK_CULL_MODE_NONE;
	rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rs.lineWidth = 1.0f;

	memset( &ms, 0, sizeof( ms ) );
	ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	ms.rasterizationSamples = layer ? xb.samples : VK_SAMPLE_COUNT_1_BIT;

	memset( &ds, 0, sizeof( ds ) );
	ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	if ( stencil != XS_OFF ) {
		ds.stencilTestEnable = VK_TRUE;
		ds.front.failOp = VK_STENCIL_OP_KEEP;
		ds.front.depthFailOp = VK_STENCIL_OP_KEEP;
		ds.front.compareMask = 0xff;
		ds.front.passOp = stencil == XS_CLIP_REPLACE ? VK_STENCIL_OP_REPLACE
			: stencil == XS_CLIP_INCREMENT ? VK_STENCIL_OP_INCREMENT_AND_CLAMP : VK_STENCIL_OP_KEEP;
		ds.front.compareOp = stencil == XS_CLIP_REPLACE ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_EQUAL;
		ds.front.writeMask = stencil == XS_TEST ? 0 : 0xff;
		ds.back = ds.front;
	}

	memset( &cba, 0, sizeof( cba ) );
	cba.colorWriteMask = ( stencil == XS_CLIP_REPLACE || stencil == XS_CLIP_INCREMENT ) ? 0 :
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if ( blend ) {	// premultiplied
		cba.blendEnable = VK_TRUE;
		cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cba.colorBlendOp = VK_BLEND_OP_ADD;
		cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		cba.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	memset( &cb, 0, sizeof( cb ) );
	cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	cb.attachmentCount = 1;
	cb.pAttachments = &cba;

	memset( &dc, 0, sizeof( dc ) );
	dc.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dc.dynamicStateCount = layer ? 3 : 2;
	dc.pDynamicStates = dyn;

	memset( &pc, 0, sizeof( pc ) );
	pc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pc.stageCount = 2;
	pc.pStages = stages;
	pc.pVertexInputState = &vi;
	pc.pInputAssemblyState = &ia;
	pc.pViewportState = &vp;
	pc.pRasterizationState = &rs;
	pc.pMultisampleState = &ms;
	pc.pDepthStencilState = &ds;
	pc.pColorBlendState = &cb;
	pc.pDynamicState = &dc;
	pc.layout = layout;
	pc.renderPass = rp;
	VK_CHECK( qvkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &pc, NULL, &p ) );
	return p;
}

static VkFormat XenUI_StencilFormat( void )
{
	const VkFormat candidates[] = { VK_FORMAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };

	for ( size_t i = 0; i < ARRAY_LEN( candidates ); i++ ) {
		VkFormatProperties props;
		qvkGetPhysicalDeviceFormatProperties( vk.physical_device, candidates[i], &props );
		if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT )
			return candidates[i];
	}
	return VK_FORMAT_UNDEFINED;
}

static void XenUI_DestroyTargets( void )
{
	int i;

	for ( i = 0; i < XENUI_MAX_LAYERS; i++ )
		XenUI_DestroyTarget( &xb.layers[i] );
	for ( i = 0; i < XENUI_MAX_PASSES; i++ )
		XenUI_DestroyTarget( &xb.bases[i] );
	xb.baseIn0 = 0;
	for ( i = 0; i < XENUI_NUM_POST; i++ )
		XenUI_DestroyTarget( &xb.postTargets[i] );
	XenUI_DestroyTarget( &xb.maskTarget );
	XenUI_DestroyImage( &xb.stencil );
	xb.numLayers = 0;
}

// a pass's base target as the picture the frame draws at the pass's RC_XENUI_END
static void XenUI_OutImage( int pass, const xenUITarget_t *t )
{
	image_t *img = &xb.outImage[pass];

	memset( img, 0, sizeof( *img ) );
	img->imgName = (char *)( pass ? "*xenuiframe1" : "*xenuiframe" );
	img->width = (word)xb.width;
	img->height = (word)xb.height;
	img->uploadWidth = xb.width;
	img->uploadHeight = xb.height;
	img->handle = t->color.image;
	img->view = t->color.view;
	img->descriptor_set = t->set;
	img->layers = 1;
	img->mipLevels = 1;
	if ( !xb.outShader[pass] )
		xb.outShader[pass] = R_CreateXenUIShader( img->imgName, img );
}

// layers[0] holds the given pass's base target (the others wait in bases[]); pass 1's is made at its first use
static qboolean XenUI_UseBase( int pass )
{
	xenUITarget_t t;

	if ( xb.baseIn0 == pass )
		return qtrue;
	if ( !xb.bases[pass].framebuffer ) {
		if ( !XenUI_Target( &xb.bases[pass], qtrue ) )
			return qfalse;
		XenUI_OutImage( pass, &xb.bases[pass] );
	}
	t = xb.layers[0];
	xb.layers[0] = xb.bases[pass];
	memset( &xb.bases[pass], 0, sizeof( xb.bases[pass] ) );
	xb.bases[xb.baseIn0] = t;
	xb.baseIn0 = pass;
	return qtrue;
}

// the window-sized targets; each pass's base layer is what the frame draws (outImage)
static qboolean XenUI_MakeTargets( void )
{
	VkImageAspectFlags aspect = VK_IMAGE_ASPECT_STENCIL_BIT;

	if ( xb.stencilFormat != VK_FORMAT_S8_UINT )
		aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
	xb.width = glConfig.vidWidth;
	xb.height = glConfig.vidHeight;
	if ( !XenUI_Image( &xb.stencil, xb.stencilFormat, xb.samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, aspect,
		xb.width, xb.height ) )
		return qfalse;
	XenUI_Barrier( xb.stencil.image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT );
	if ( !XenUI_Target( &xb.layers[0], qtrue ) )
		return qfalse;
	xb.numLayers = 1;
	for ( int i = 0; i < XENUI_NUM_POST; i++ ) {
		if ( !XenUI_Target( &xb.postTargets[i], qfalse ) )
			return qfalse;
	}
	if ( !XenUI_Target( &xb.maskTarget, qfalse ) )
		return qfalse;

	xb.baseIn0 = 0;
	XenUI_OutImage( 0, &xb.layers[0] );
	return qtrue;
}

static qboolean XenUI_Init( void )
{
	VkCommandPoolCreateInfo			cp;
	VkCommandBufferAllocateInfo		ca;
	VkFenceCreateInfo				fc;
	VkDescriptorPoolSize			sizes[2];
	VkDescriptorPoolCreateInfo		dp;
	VkDescriptorSetLayoutBinding	ub;
	VkDescriptorSetLayoutCreateInfo	dl;
	VkPipelineLayoutCreateInfo		pl;
	VkDescriptorSetLayout			sets[2];
	VkPushConstantRange				push;
	VkSamplerCreateInfo				sc;
	int								i, s;

	xb.samples = (VkSampleCountFlagBits)( vk.msaaActive && vkSamples > 1 ? vkSamples : 1 );
	xb.stencilFormat = XenUI_StencilFormat();
	xb.setStencilReference = (PFN_vkCmdSetStencilReference)qvkGetDeviceProcAddr( vk.device, "vkCmdSetStencilReference" );
	if ( xb.stencilFormat == VK_FORMAT_UNDEFINED || !xb.setStencilReference ) {
		ri.Printf( PRINT_WARNING, "xenui: no stencil format: clip masks off\n" );
		return qfalse;
	}

	memset( &cp, 0, sizeof( cp ) );
	cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cp.queueFamilyIndex = vk.queue_family_index;
	VK_CHECK( qvkCreateCommandPool( vk.device, &cp, NULL, &xb.pool ) );

	memset( &sizes, 0, sizeof( sizes ) );
	sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	sizes[0].descriptorCount = XENUI_MAX_TEXTURES + 64;
	sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	sizes[1].descriptorCount = NUM_COMMAND_BUFFERS;
	memset( &dp, 0, sizeof( dp ) );
	dp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	dp.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	dp.maxSets = XENUI_MAX_TEXTURES + 64 + NUM_COMMAND_BUFFERS;
	dp.poolSizeCount = 2;
	dp.pPoolSizes = sizes;
	VK_CHECK( qvkCreateDescriptorPool( vk.device, &dp, NULL, &xb.descriptorPool ) );

	memset( &ub, 0, sizeof( ub ) );
	ub.binding = 0;
	ub.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	ub.descriptorCount = 1;
	ub.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	memset( &dl, 0, sizeof( dl ) );
	dl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dl.bindingCount = 1;
	dl.pBindings = &ub;
	VK_CHECK( qvkCreateDescriptorSetLayout( vk.device, &dl, NULL, &xb.uboLayout ) );

	push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	push.offset = 0;
	push.size = 128;
	memset( &pl, 0, sizeof( pl ) );
	pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pl.setLayoutCount = 2;
	pl.pSetLayouts = sets;
	pl.pushConstantRangeCount = 1;
	pl.pPushConstantRanges = &push;
	sets[0] = vk.set_layout_sampler;
	sets[1] = xb.uboLayout;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &pl, NULL, &xb.geomLayout ) );
	sets[1] = vk.set_layout_sampler;
	VK_CHECK( qvkCreatePipelineLayout( vk.device, &pl, NULL, &xb.postLayout ) );

	memset( &sc, 0, sizeof( sc ) );
	sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sc.magFilter = VK_FILTER_LINEAR;
	sc.minFilter = VK_FILTER_LINEAR;
	sc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sc.addressModeU = sc.addressModeV = sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sc.maxLod = 0.25f;
	VK_CHECK( qvkCreateSampler( vk.device, &sc, NULL, &xb.sampler ) );

	xb.rpLayer = XenUI_RenderPass( qtrue, qfalse );
	xb.rpLayerClear = XenUI_RenderPass( qtrue, qtrue );
	xb.rpPost = XenUI_RenderPass( qfalse, qfalse );

	xb.geomVert = XenUI_Shader( xenui_geom_vert_spv, xenui_geom_vert_spv_size );
	xb.quadVert = XenUI_Shader( xenui_quad_vert_spv, xenui_quad_vert_spv_size );
	xb.colorFrag = XenUI_Shader( xenui_color_frag_spv, xenui_color_frag_spv_size );
	xb.textureFrag = XenUI_Shader( xenui_texture_frag_spv, xenui_texture_frag_spv_size );
	xb.gradientFrag = XenUI_Shader( xenui_gradient_frag_spv, xenui_gradient_frag_spv_size );
	xb.postFrag = XenUI_Shader( xenui_post_frag_spv, xenui_post_frag_spv_size );

	for ( s = 0; s < 2; s++ ) {
		const int stencil = s ? XS_TEST : XS_OFF;
		xb.geom[XP_COLOR][s] = XenUI_Pipeline( qtrue, xb.colorFrag, xb.geomLayout, xb.rpLayer, qtrue, qtrue, stencil );
		xb.geom[XP_TEXTURE][s] = XenUI_Pipeline( qtrue, xb.textureFrag, xb.geomLayout, xb.rpLayer, qtrue, qtrue, stencil );
		xb.geom[XP_GRADIENT][s] = XenUI_Pipeline( qtrue, xb.gradientFrag, xb.geomLayout, xb.rpLayer, qtrue, qtrue, stencil );
		xb.composite[XENUI_BLEND][s] = XenUI_Pipeline( qfalse, xb.postFrag, xb.postLayout, xb.rpLayer, qtrue, qtrue, stencil );
		xb.composite[XENUI_BLEND_REPLACE][s] = XenUI_Pipeline( qfalse, xb.postFrag, xb.postLayout, xb.rpLayer, qtrue, qfalse, stencil );
	}
	xb.clipReplace = XenUI_Pipeline( qtrue, xb.colorFrag, xb.geomLayout, xb.rpLayer, qtrue, qfalse, XS_CLIP_REPLACE );
	xb.clipIncrement = XenUI_Pipeline( qtrue, xb.colorFrag, xb.geomLayout, xb.rpLayer, qtrue, qfalse, XS_CLIP_INCREMENT );
	xb.post = XenUI_Pipeline( qfalse, xb.postFrag, xb.postLayout, xb.rpPost, qfalse, qfalse, XS_OFF );
	xb.postBlend = XenUI_Pipeline( qfalse, xb.postFrag, xb.postLayout, xb.rpPost, qfalse, qtrue, XS_OFF );

	{	// optional: GPU time
		PFN_vkCreateQueryPool createQueryPool = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCreateQueryPool" );
		VkPhysicalDeviceProperties props;
		qvkGetPhysicalDeviceProperties( vk.physical_device, &props );
		xb.resetQueryPool = (PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr( vk.device, "vkCmdResetQueryPool" );
		xb.writeTimestamp = (PFN_vkCmdWriteTimestamp)qvkGetDeviceProcAddr( vk.device, "vkCmdWriteTimestamp" );
		xb.queryResults = (PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr( vk.device, "vkGetQueryPoolResults" );
		xb.nsPerTick = props.limits.timestampPeriod;
		if ( createQueryPool && xb.resetQueryPool && xb.writeTimestamp && xb.queryResults && xb.nsPerTick > 0 ) {
			VkQueryPoolCreateInfo qc;
			memset( &qc, 0, sizeof( qc ) );
			qc.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
			qc.queryType = VK_QUERY_TYPE_TIMESTAMP;
			qc.queryCount = 2 * NUM_COMMAND_BUFFERS;
			if ( createQueryPool( vk.device, &qc, NULL, &xb.queries ) != VK_SUCCESS )
				xb.queries = VK_NULL_HANDLE;
		}
	}

	for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
		xenUISlot_t				*slot = &xb.slots[i];
		VkBufferCreateInfo		bc;
		VkMemoryRequirements	req;
		VkMemoryAllocateInfo	ai;
		VkDescriptorSetAllocateInfo	sa;
		VkDescriptorBufferInfo	bi;
		VkWriteDescriptorSet	w;

		memset( &ca, 0, sizeof( ca ) );
		ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		ca.commandPool = xb.pool;
		ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		ca.commandBufferCount = 1;
		VK_CHECK( qvkAllocateCommandBuffers( vk.device, &ca, &slot->cmd ) );
		memset( &fc, 0, sizeof( fc ) );
		fc.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		VK_CHECK( qvkCreateFence( vk.device, &fc, NULL, &slot->fence ) );

		memset( &bc, 0, sizeof( bc ) );
		bc.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bc.size = XENUI_STREAM_SIZE;
		bc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bc.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VK_CHECK( qvkCreateBuffer( vk.device, &bc, NULL, &slot->stream ) );
		qvkGetBufferMemoryRequirements( vk.device, slot->stream, &req );
		memset( &ai, 0, sizeof( ai ) );
		ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		ai.allocationSize = req.size;
		ai.memoryTypeIndex = vk_find_memory_type( req.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
		VK_CHECK( vk_vram_allocate_named( vk.device, &ai, &slot->streamMemory, "xenui" ) );
		VK_CHECK( qvkBindBufferMemory( vk.device, slot->stream, slot->streamMemory, 0 ) );
		VK_CHECK( qvkMapMemory( vk.device, slot->streamMemory, 0, VK_WHOLE_SIZE, 0, (void **)&slot->streamPtr ) );

		memset( &sa, 0, sizeof( sa ) );
		sa.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		sa.descriptorPool = xb.descriptorPool;
		sa.descriptorSetCount = 1;
		sa.pSetLayouts = &xb.uboLayout;
		VK_CHECK( qvkAllocateDescriptorSets( vk.device, &sa, &slot->ubo ) );
		bi.buffer = slot->stream;
		bi.offset = 0;
		bi.range = XENUI_GRADIENT_RANGE;
		memset( &w, 0, sizeof( w ) );
		w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		w.dstSet = slot->ubo;
		w.descriptorCount = 1;
		w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		w.pBufferInfo = &bi;
		qvkUpdateDescriptorSets( vk.device, 1, &w, 0, NULL );
	}
	return qtrue;
}

static void XenUI_ReleaseOwnTexture( xenUITexture_t *t )
{
	if ( t->set )
		qvkFreeDescriptorSets( vk.device, xb.descriptorPool, 1, &t->set );
	if ( t->ownView )
		qvkDestroyImageView( vk.device, t->ownView, NULL );
	if ( t->own )
		qvkDestroyImage( vk.device, t->own, NULL );
	if ( t->ownMemory )
		qvkFreeMemory( vk.device, t->ownMemory, NULL );
	memset( t, 0, sizeof( *t ) );
}

void R_XenUIShutdown( void )
{
	int i;

	if ( xb.ready || xb.pool ) {
		qvkDeviceWaitIdle( vk.device );
		XenUI_DestroyTargets();
		for ( i = 1; i < XENUI_MAX_TEXTURES; i++ ) {
			if ( xenTextures[i].own )
				XenUI_ReleaseOwnTexture( &xenTextures[i] );
		}
		for ( i = 0; i < NUM_COMMAND_BUFFERS; i++ ) {
			xenUISlot_t *slot = &xb.slots[i];
			if ( slot->fence )
				qvkDestroyFence( vk.device, slot->fence, NULL );
			if ( slot->streamPtr )
				qvkUnmapMemory( vk.device, slot->streamMemory );
			if ( slot->stream )
				qvkDestroyBuffer( vk.device, slot->stream, NULL );
			if ( slot->streamMemory )
				qvkFreeMemory( vk.device, slot->streamMemory, NULL );
		}
		VkPipeline *pipes[] = { &xb.geom[0][0], &xb.geom[0][1], &xb.geom[1][0], &xb.geom[1][1], &xb.geom[2][0],
			&xb.geom[2][1], &xb.clipReplace, &xb.clipIncrement, &xb.composite[0][0], &xb.composite[0][1],
			&xb.composite[1][0], &xb.composite[1][1], &xb.post, &xb.postBlend };
		for ( i = 0; i < (int)ARRAY_LEN( pipes ); i++ ) {
			if ( *pipes[i] )
				qvkDestroyPipeline( vk.device, *pipes[i], NULL );
		}
		VkShaderModule mods[] = { xb.geomVert, xb.quadVert, xb.colorFrag, xb.textureFrag, xb.gradientFrag, xb.postFrag };
		for ( i = 0; i < (int)ARRAY_LEN( mods ); i++ ) {
			if ( mods[i] )
				qvkDestroyShaderModule( vk.device, mods[i], NULL );
		}
		VkRenderPass rps[] = { xb.rpLayer, xb.rpLayerClear, xb.rpPost };
		for ( i = 0; i < (int)ARRAY_LEN( rps ); i++ ) {
			if ( rps[i] )
				qvkDestroyRenderPass( vk.device, rps[i], NULL );
		}
		if ( xb.geomLayout )
			qvkDestroyPipelineLayout( vk.device, xb.geomLayout, NULL );
		if ( xb.postLayout )
			qvkDestroyPipelineLayout( vk.device, xb.postLayout, NULL );
		if ( xb.uboLayout )
			qvkDestroyDescriptorSetLayout( vk.device, xb.uboLayout, NULL );
		if ( xb.sampler )
			qvkDestroySampler( vk.device, xb.sampler, NULL );
		if ( xb.queries ) {
			PFN_vkDestroyQueryPool destroyQueryPool = (PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr( vk.device, "vkDestroyQueryPool" );
			if ( destroyQueryPool )
				destroyQueryPool( vk.device, xb.queries, NULL );
		}
		if ( xb.descriptorPool )
			qvkDestroyDescriptorPool( vk.device, xb.descriptorPool, NULL );
		if ( xb.pool )
			qvkDestroyCommandPool( vk.device, xb.pool, NULL );
	}
	memset( &xb, 0, sizeof( xb ) );

	for ( i = 1; i < XENUI_MAX_GEOMETRY; i++ )
		XenUI_FreeGeometry( i );
	memset( xenTextures, 0, sizeof( xenTextures ) );
	memset( xenFilters, 0, sizeof( xenFilters ) );
	memset( xenShaders, 0, sizeof( xenShaders ) );
	memset( &xf, 0, sizeof( xf ) );
}

// --- backend: recording ----------------------------------------------------------------------------------------------

static VkCommandBuffer XB( void )
{
	return xb.slots[xb.slot].cmd;
}

static void XenUI_EndPass( void )
{
	if ( xb.open >= 0 ) {
		qvkCmdEndRenderPass( XB() );
		xb.open = -1;
		xb.bound = VK_NULL_HANDLE;
	}
}

static VkRect2D XenUI_Scissor( void )
{
	VkRect2D r;

	if ( xb.scissor )
		return xb.scissorRect;
	r.offset.x = r.offset.y = 0;
	r.extent.width = xb.width;
	r.extent.height = xb.height;
	return r;
}

static void XenUI_Viewport( void )
{
	VkViewport v;

	v.x = v.y = 0;
	v.width = (float)xb.width;
	v.height = (float)xb.height;
	v.minDepth = 0;
	v.maxDepth = 1;
	qvkCmdSetViewport( XB(), 0, 1, &v );
}

static void XenUI_BeginPass( VkRenderPass rp, VkFramebuffer fb, qboolean clear )
{
	VkRenderPassBeginInfo	bi;
	VkClearValue			cv[3];

	memset( cv, 0, sizeof( cv ) );
	memset( &bi, 0, sizeof( bi ) );
	bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	bi.renderPass = rp;
	bi.framebuffer = fb;
	bi.renderArea.extent.width = xb.width;
	bi.renderArea.extent.height = xb.height;
	bi.clearValueCount = clear ? 3 : 0;
	bi.pClearValues = clear ? cv : NULL;
	qvkCmdBeginRenderPass( XB(), &bi, VK_SUBPASS_CONTENTS_INLINE );
	XenUI_Viewport();
	xb.bound = VK_NULL_HANDLE;
}

// the top layer's pass, open
static void XenUI_OpenTop( void )
{
	if ( xb.open == xb.top )
		return;
	XenUI_EndPass();
	XenUI_BeginPass( xb.rpLayer, xb.layers[xb.top].framebuffer, qfalse );
	xb.open = xb.top;
}

static void XenUI_Bind( VkPipeline p )
{
	if ( xb.bound != p ) {
		qvkCmdBindPipeline( XB(), VK_PIPELINE_BIND_POINT_GRAPHICS, p );
		xb.bound = p;
	}
}

static void XenUI_Push( const void *data, uint32_t size )
{
	qvkCmdPushConstants( XB(), xb.geomLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data );
}

// the slot's stream: n bytes at an offset aligned for every use, or NULL when full
static byte *XenUI_Stream( uint32_t n, uint32_t *offset )
{
	xenUISlot_t		*slot = &xb.slots[xb.slot];
	const uint32_t	at = ( slot->streamUsed + 255u ) & ~255u;

	if ( at + n > XENUI_STREAM_SIZE ) {
		static int warned;
		if ( warned++ < 3 )
			ri.Printf( PRINT_WARNING, "xenui: stream buffer full (%u bytes)\n", XENUI_STREAM_SIZE );
		return NULL;
	}
	slot->streamUsed = at + n;
	*offset = at;
	return slot->streamPtr + at;
}

// window pixels to clip space after the element's transform
static void XenUI_GeometryPush( float tx, float ty )
{
	struct { float m[16]; float t[2]; } p;
	float proj[16], *m = p.m;
	int r, c;

	memset( proj, 0, sizeof( proj ) );
	proj[0] = 2.0f / xb.width;
	proj[5] = 2.0f / xb.height;
	proj[10] = 1.0f;
	proj[12] = -1.0f;
	proj[13] = -1.0f;
	proj[15] = 1.0f;
	if ( xb.transform ) {	// proj * transform, column major
		for ( c = 0; c < 4; c++ )
			for ( r = 0; r < 4; r++ )
				m[c * 4 + r] = proj[0 * 4 + r] * xb.m[c * 4 + 0] + proj[1 * 4 + r] * xb.m[c * 4 + 1] +
					proj[2 * 4 + r] * xb.m[c * 4 + 2] + proj[3 * 4 + r] * xb.m[c * 4 + 3];
	} else {
		memcpy( m, proj, sizeof( proj ) );
	}
	p.t[0] = tx;
	p.t[1] = ty;
	XenUI_Push( &p, sizeof( p ) );
}

static qboolean XenUI_DrawGeometry( int geometry, float tx, float ty )
{
	const xenUIGeometry_t	*g = &xenGeometry[geometry];
	const uint32_t			vbytes = g->numVerts * sizeof( xenUIVertex_t ), ibytes = g->numIndexes * sizeof( int );
	uint32_t				voff, ioff;
	byte					*v, *i;
	VkBuffer				stream = xb.slots[xb.slot].stream;
	VkDeviceSize			off;
	VkRect2D				sc = XenUI_Scissor();

	if ( !g->verts || !( v = XenUI_Stream( vbytes, &voff ) ) || !( i = XenUI_Stream( ibytes, &ioff ) ) )
		return qfalse;
	memcpy( v, g->verts, vbytes );
	memcpy( i, g->indexes, ibytes );
	off = voff;
	qvkCmdBindVertexBuffers( XB(), 0, 1, &stream, &off );
	qvkCmdBindIndexBuffer( XB(), stream, ioff, VK_INDEX_TYPE_UINT32 );
	qvkCmdSetScissor( XB(), 0, 1, &sc );
	XenUI_GeometryPush( tx, ty );
	qvkCmdDrawIndexed( XB(), g->numIndexes, 1, 0, 0, 0 );
	return qtrue;
}

static void XenUI_BindSets( VkPipelineLayout layout, VkDescriptorSet set0, VkDescriptorSet set1, qboolean dynamic, uint32_t offset )
{
	VkDescriptorSet sets[2] = { set0, set1 };
	qvkCmdBindDescriptorSets( XB(), VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, sets, dynamic ? 1 : 0, &offset );
}

// set 0 of untextured draws and set 1 of passes without a mask: the engine's white image
static void XenUI_White( void )
{
	xb.whiteSet = tr.whiteImage->descriptor_set;
}

static void XB_Geometry( const xenUICommand_t *c, int kind, int shader )
{
	VkDescriptorSet	set0, set1 = xb.slots[xb.slot].ubo;
	uint32_t		uoff = 0;

	XenUI_OpenTop();
	XenUI_White();
	set0 = kind == XP_TEXTURE && c->b ? xenTextures[c->b].set : xb.whiteSet;
	if ( !set0 )
		return;
	if ( kind == XP_GRADIENT ) {	// the gradient block, as xenui_gradient.frag reads it
		const xenUIShader_t *s = &xenShaders[shader].s;
		struct { int func, repeating, num, pad; float p[2], v[2]; float colors[XENUI_MAX_STOPS][4]; float pos[XENUI_MAX_STOPS]; } u;
		static_assert( sizeof( u ) <= XENUI_GRADIENT_RANGE, "the gradient block outgrew its range" );
		byte *dst = XenUI_Stream( XENUI_GRADIENT_RANGE, &uoff );	// the bound range must lie inside the buffer
		if ( !dst )
			return;
		memset( &u, 0, sizeof( u ) );
		u.func = s->type;
		u.repeating = s->repeating;
		u.num = Q_min( s->numStops, XENUI_MAX_STOPS );
		memcpy( u.p, s->p, sizeof( u.p ) );
		memcpy( u.v, s->v, sizeof( u.v ) );
		memcpy( u.colors, s->stopColors, sizeof( u.colors ) );
		memcpy( u.pos, s->stopPositions, sizeof( u.pos ) );
		memcpy( dst, &u, sizeof( u ) );
	}
	XenUI_Bind( xb.geom[kind][xb.clip ? 1 : 0] );
	if ( xb.clip )
		xb.setStencilReference( XB(), VK_STENCIL_FACE_FRONT_AND_BACK, xb.stencilRef );
	XenUI_BindSets( xb.geomLayout, set0, set1, qtrue, uoff );
	XenUI_DrawGeometry( c->a, c->tx, c->ty );
}

static void XenUI_ClearStencil( uint32_t value )
{
	VkClearAttachment	ca;
	VkClearRect			cr;

	memset( &ca, 0, sizeof( ca ) );
	ca.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT;
	ca.clearValue.depthStencil.stencil = value;
	memset( &cr, 0, sizeof( cr ) );
	cr.rect.extent.width = xb.width;
	cr.rect.extent.height = xb.height;
	cr.layerCount = 1;
	qvkCmdClearAttachments( XB(), 1, &ca, 1, &cr );
}

// The mask is the stencil value stencilRef. Set takes a new value (no clear: older values fail the test); Intersect
// steps the value where the mask holds; SetInverse clears to a new value and writes 0 under the geometry. The frame
// starts at 0, and a mask past 254 clears and starts again.
static void XB_ClipRender( const xenUICommand_t *c )
{
	XenUI_OpenTop();
	XenUI_White();
	if ( c->a == XENUI_CLIP_INTERSECT && xb.stencilRef > 0 && xb.stencilRef < 255 ) {
		XenUI_Bind( xb.clipIncrement );
		xb.setStencilReference( XB(), VK_STENCIL_FACE_FRONT_AND_BACK, xb.stencilRef );
		xb.stencilRef++;
	} else {
		if ( xb.stencilRef >= 254 ) {
			XenUI_ClearStencil( 0 );
			xb.stencilRef = 0;
		}
		xb.stencilRef++;
		XenUI_Bind( xb.clipReplace );
		if ( c->a == XENUI_CLIP_SET_INVERSE ) {
			XenUI_ClearStencil( xb.stencilRef );
			xb.setStencilReference( XB(), VK_STENCIL_FACE_FRONT_AND_BACK, 0 );
		} else {	// Set, or an Intersect with no mask to intersect
			xb.setStencilReference( XB(), VK_STENCIL_FACE_FRONT_AND_BACK, xb.stencilRef );
		}
	}
	XenUI_BindSets( xb.geomLayout, xb.whiteSet, xb.slots[xb.slot].ubo, qtrue, 0 );
	XenUI_DrawGeometry( c->b, c->tx, c->ty );
}

typedef struct {
	float	m[16];
	float	a[4];
	float	color[4];
	float	limits[4];
	float	invSize[2];
	int		mode;
	int		pad;
} xenUIPostPush_t;

static void XenUI_PostPush( xenUIPostPush_t *p, int mode )
{
	const VkRect2D r = XenUI_Scissor();

	p->mode = mode;
	p->invSize[0] = 1.0f / xb.width;
	p->invSize[1] = 1.0f / xb.height;
	p->limits[0] = ( r.offset.x + 0.5f ) / xb.width;
	p->limits[1] = ( r.offset.y + 0.5f ) / xb.height;
	p->limits[2] = ( r.offset.x + r.extent.width - 0.5f ) / xb.width;
	p->limits[3] = ( r.offset.y + r.extent.height - 0.5f ) / xb.height;
	XenUI_Push( p, sizeof( *p ) );
}

// one full-screen pass from a sampled image into a post target, inside the scissor
static void XenUI_PostPass( VkDescriptorSet src, xenUITarget_t *dst, xenUIPostPush_t *p, int mode, qboolean blend )
{
	VkRect2D sc = XenUI_Scissor();

	XenUI_EndPass();
	XenUI_BeginPass( xb.rpPost, dst->framebuffer, qfalse );
	xb.open = XENUI_MAX_LAYERS;	// a post target: XenUI_EndPass ends it
	XenUI_Bind( blend ? xb.postBlend : xb.post );
	XenUI_BindSets( xb.postLayout, src, mode == 4 ? xb.maskTarget.set : xb.whiteSet, qfalse, 0 );
	qvkCmdSetScissor( XB(), 0, 1, &sc );
	XenUI_PostPush( p, mode );
	qvkCmdDraw( XB(), 3, 1, 0, 0 );
	XenUI_EndPass();
}

static void XenUI_Copy( VkDescriptorSet src, xenUITarget_t *dst, float alpha )
{
	xenUIPostPush_t p;

	memset( &p, 0, sizeof( p ) );
	p.color[0] = p.color[1] = p.color[2] = p.color[3] = alpha;
	XenUI_PostPass( src, dst, &p, 0, qfalse );
}

// a gaussian on post target *cur through *other, back into *cur
static void XenUI_Blur( int cur, int other, float sigma )
{
	xenUIPostPush_t	p;
	const float		reach = 3.0f * sigma;
	const int		taps = (int)Q_min( ceilf( reach ), 48.0f );
	const float		step = taps > 0 ? Q_max( 1.0f, reach / taps ) : 1.0f;

	if ( sigma <= 0.0f || taps <= 0 )
		return;
	memset( &p, 0, sizeof( p ) );
	p.a[2] = sigma / step;
	p.a[3] = (float)taps;
	p.a[0] = step;
	XenUI_PostPass( xb.postTargets[cur].set, &xb.postTargets[other], &p, 2, qfalse );
	p.a[0] = 0;
	p.a[1] = step;
	XenUI_PostPass( xb.postTargets[other].set, &xb.postTargets[cur], &p, 2, qfalse );
}

static void XB_Composite( const xenUICommand_t *c )
{
	const int	src = Q_min( Q_max( c->a, 0 ), xb.numLayers - 1 ), dst = Q_min( Q_max( c->b, 0 ), xb.numLayers - 1 );
	int			cur = 0, i;
	VkRect2D	sc;
	xenUIPostPush_t p;

	XenUI_EndPass();	// the source's pass resolves it
	XenUI_White();
	XenUI_Copy( xb.layers[src].set, &xb.postTargets[0], 1.0f );

	for ( i = 0; i < c->d; i++ ) {
		const xenUIFilterSlot_t	*fs = &xenFilters[c->filters[i]];
		const xenUIFilter_t		*f = &fs->f;
		const int				other = ( cur + 1 ) % XENUI_NUM_POST, third = ( cur + 2 ) % XENUI_NUM_POST;

		memset( &p, 0, sizeof( p ) );
		if ( fs->mask ) {
			XenUI_PostPass( xb.postTargets[cur].set, &xb.postTargets[other], &p, 4, qfalse );
			cur = other;
		} else if ( f->type == XENUI_FILTER_OPACITY ) {
			XenUI_Copy( xb.postTargets[cur].set, &xb.postTargets[other], f->value );
			cur = other;
		} else if ( f->type == XENUI_FILTER_COLOR_MATRIX ) {
			memcpy( p.m, f->matrix, sizeof( p.m ) );
			XenUI_PostPass( xb.postTargets[cur].set, &xb.postTargets[other], &p, 1, qfalse );
			cur = other;
		} else if ( f->type == XENUI_FILTER_BLUR ) {
			XenUI_Blur( cur, other, f->sigma );
		} else if ( f->type == XENUI_FILTER_DROP_SHADOW ) {	// the tinted, offset alpha, blurred, under the source
			p.a[0] = f->offset[0];
			p.a[1] = f->offset[1];
			memcpy( p.color, f->color, sizeof( p.color ) );
			XenUI_PostPass( xb.postTargets[cur].set, &xb.postTargets[other], &p, 3, qfalse );
			XenUI_Blur( other, third, f->sigma );
			memset( &p, 0, sizeof( p ) );
			p.color[0] = p.color[1] = p.color[2] = p.color[3] = 1.0f;
			XenUI_PostPass( xb.postTargets[cur].set, &xb.postTargets[other], &p, 0, qtrue );
			cur = other;
		}
	}

	// into the destination, through the clip mask
	XenUI_EndPass();
	XenUI_BeginPass( xb.rpLayer, xb.layers[dst].framebuffer, qfalse );
	xb.open = dst;
	XenUI_Bind( xb.composite[c->c == XENUI_BLEND_REPLACE ? 1 : 0][xb.clip ? 1 : 0] );
	if ( xb.clip )
		xb.setStencilReference( XB(), VK_STENCIL_FACE_FRONT_AND_BACK, xb.stencilRef );
	XenUI_BindSets( xb.postLayout, xb.postTargets[cur].set, xb.whiteSet, qfalse, 0 );
	sc = XenUI_Scissor();
	qvkCmdSetScissor( XB(), 0, 1, &sc );
	memset( &p, 0, sizeof( p ) );
	p.color[0] = p.color[1] = p.color[2] = p.color[3] = 1.0f;
	XenUI_PostPush( &p, 0 );
	qvkCmdDraw( XB(), 3, 1, 0, 0 );
}

static void XB_Push( void )
{
	VkClearAttachment	ca;
	VkClearRect			cr;

	if ( xb.top + 1 >= XENUI_MAX_LAYERS ) {
		xb.refused++;
		return;
	}
	XenUI_EndPass();
	if ( xb.top + 1 >= xb.numLayers ) {
		if ( !XenUI_Target( &xb.layers[xb.numLayers], qtrue ) ) {
			ri.Printf( PRINT_WARNING, "xenui: could not make layer %d\n", xb.numLayers );
			xb.refused++;
			return;
		}
		xb.numLayers++;
	}
	xb.top++;
	XenUI_OpenTop();
	memset( &ca, 0, sizeof( ca ) );	// transparent inside the scissor
	ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cr.rect = XenUI_Scissor();
	cr.baseArrayLayer = 0;
	cr.layerCount = 1;
	qvkCmdClearAttachments( XB(), 1, &ca, 1, &cr );
}

static void XB_SaveTexture( const xenUICommand_t *c )
{
	xenUITexture_t	*t = &xenTextures[c->a];
	xenUIImage_t	img;
	VkImageCopy		region;
	const VkRect2D	r = XenUI_Scissor();

	if ( !r.extent.width || !r.extent.height )
		return;	// the scissor is off the window: nothing to save (the texture keeps no set)
	XenUI_EndPass();
	memset( &img, 0, sizeof( img ) );
	if ( !XenUI_Image( &img, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, r.extent.width, r.extent.height ) ) {
		XenUI_DestroyImage( &img );
		return;
	}
	const VkImage src = xb.layers[xb.top].color.image;
	XenUI_Barrier( src, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT );
	XenUI_Barrier( img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT );
	memset( &region, 0, sizeof( region ) );
	region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.srcSubresource.layerCount = region.dstSubresource.layerCount = 1;
	region.srcOffset.x = r.offset.x;
	region.srcOffset.y = r.offset.y;
	region.extent.width = r.extent.width;
	region.extent.height = r.extent.height;
	region.extent.depth = 1;
	qvkCmdCopyImage( XB(), src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
	XenUI_Barrier( src, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0 );
	XenUI_Barrier( img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT );

	t->own = img.image;
	t->ownMemory = img.memory;
	t->ownView = img.view;
	t->set = XenUI_SamplerSet( img.view );
	t->width = r.extent.width;
	t->height = r.extent.height;
}

static void XB_SaveMask( void )
{
	XenUI_EndPass();
	XenUI_White();
	XenUI_Copy( xb.layers[xb.top].set, &xb.maskTarget, 1.0f );
}

// the slot's command buffer, begun: the first xenui command of a frame
static qboolean XenUI_Begin( void )
{
	VkCommandBufferBeginInfo	bi;
	VkMemoryBarrier				mb;
	xenUISlot_t					*slot;
	int							i;

	if ( xb.failed )
		return qfalse;
	if ( !xb.ready ) {
		if ( !XenUI_Init() ) {
			xb.failed = qtrue;
			return qfalse;
		}
		xb.ready = qtrue;
		xb.open = -1;
	}
	if ( xb.recording ) {	// a frame that recorded and never submitted (vk_end_frame returned early): drop it
		XenUI_EndPass();
		qvkEndCommandBuffer( XB() );
		xb.recording = qfalse;
	}

	xb.slot = vk.cmd_index;
	slot = &xb.slots[xb.slot];
	if ( slot->submitted ) {
		VK_CHECK( qvkWaitForFences( vk.device, 1, &slot->fence, VK_TRUE, 1e10 ) );
		VK_CHECK( qvkResetFences( vk.device, 1, &slot->fence ) );
		slot->submitted = qfalse;
		if ( xb.timed[xb.slot] ) {
			uint64_t ticks[2];
			if ( xb.queryResults( vk.device, xb.queries, 2 * xb.slot, 2, sizeof( ticks ), ticks, sizeof( uint64_t ),
				VK_QUERY_RESULT_64_BIT ) == VK_SUCCESS )
				xb.gpuMs = (float)( ( ticks[1] - ticks[0] ) * xb.nsPerTick * 1e-6 );
		}
	}
	xb.timed[xb.slot] = qfalse;
	xb.frame++;
	for ( i = 1; i < XENUI_MAX_TEXTURES; i++ ) {	// released saved layers no slot can still read
		if ( xenTextures[i].own && !xenTextures[i].used && xenTextures[i].freeAt && xenTextures[i].freeAt <= xb.frame )
			XenUI_ReleaseOwnTexture( &xenTextures[i] );
	}

	VK_CHECK( qvkResetCommandBuffer( slot->cmd, 0 ) );
	memset( &bi, 0, sizeof( bi ) );
	bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( qvkBeginCommandBuffer( slot->cmd, &bi ) );
	xb.recording = qtrue;
	xb.recFrame = tr.frameCount;
	xb.pass = 0;
	xb.passOpen = qfalse;
	slot->streamUsed = 0;

	// the frames before read the base layer (RC_XENUI_END) and the layers: done before this frame writes them
	memset( &mb, 0, sizeof( mb ) );
	mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	qvkCmdPipelineBarrier( slot->cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb,
		0, NULL, 0, NULL );
	if ( xb.queries ) {
		xb.resetQueryPool( slot->cmd, xb.queries, 2 * xb.slot, 2 );
		xb.writeTimestamp( slot->cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, xb.queries, 2 * xb.slot );
	}

	if ( xb.width != glConfig.vidWidth || xb.height != glConfig.vidHeight || !xb.numLayers ) {
		if ( xb.numLayers ) {
			qvkDeviceWaitIdle( vk.device );
			XenUI_DestroyTargets();
		}
		if ( !XenUI_MakeTargets() ) {
			ri.Printf( PRINT_WARNING, "xenui: could not make the layers\n" );
			xb.failed = qtrue;
			qvkEndCommandBuffer( slot->cmd );
			xb.recording = qfalse;
			return qfalse;
		}
	}

	return qtrue;
}

// a document pass: its base layer in layers[0], cleared with the mask
static qboolean XenUI_BeginDocument( void )
{
	if ( xb.pass >= XENUI_MAX_PASSES )
		return qfalse;
	if ( xb.pass > 0 ) {	// the passes before read the stencil and scratch images this one writes
		VkMemoryBarrier mb;
		memset( &mb, 0, sizeof( mb ) );
		mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_TRANSFER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		qvkCmdPipelineBarrier( XB(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb,
			0, NULL, 0, NULL );
	}
	if ( !XenUI_UseBase( xb.pass ) ) {
		ri.Printf( PRINT_WARNING, "xenui: could not make the base layer of pass %d\n", xb.pass );
		xb.pass = XENUI_MAX_PASSES;
		return qfalse;
	}
	xb.top = 0;
	xb.refused = 0;
	xb.scissor = qfalse;
	xb.transform = qfalse;
	xb.clip = qfalse;
	xb.stencilRef = 0;
	XenUI_BeginPass( xb.rpLayerClear, xb.layers[0].framebuffer, qtrue );	// transparent, mask cleared
	xb.open = 0;
	xb.passOpen = qtrue;
	return qtrue;
}

const void *RB_XenUI( const void *data )
{
	const xenUICommand_t *c = (const xenUICommand_t *)data;

	if ( c->op == XOP_RELEASE_GEOMETRY ) {	// the draws before it in this frame copied its vertices already
		XenUI_FreeGeometry( c->a );
		return (const void *)( c + 1 );
	}
	if ( c->op == XOP_RELEASE_TEXTURE ) {	// freed once no slot in flight can read it
		xenTextures[c->a].freeAt = xb.frame + NUM_COMMAND_BUFFERS + 1;
		return (const void *)( c + 1 );
	}
	if ( c->op == XOP_END && !( xb.recording && xb.passOpen && xb.recFrame == tr.frameCount ) )
		return (const void *)( c + 1 );
	if ( ( !xb.recording || xb.recFrame != tr.frameCount ) && !XenUI_Begin() )
		return (const void *)( c + 1 );
	if ( !xb.passOpen && !XenUI_BeginDocument() )
		return (const void *)( c + 1 );

	switch ( c->op ) {
	case XOP_GEOMETRY:
		XB_Geometry( c, c->b ? XP_TEXTURE : XP_COLOR, 0 );
		break;
	case XOP_SHADER:
		XB_Geometry( c, XP_GRADIENT, c->b );
		break;
	case XOP_SCISSOR:
		xb.scissor = (qboolean)( c->a != 0 );
		xb.scissorRect.offset.x = Q_max( 0, c->b );
		xb.scissorRect.offset.y = Q_max( 0, c->c );
		xb.scissorRect.extent.width = Q_max( 0, Q_min( c->b + c->d, xb.width ) - xb.scissorRect.offset.x );
		xb.scissorRect.extent.height = Q_max( 0, Q_min( c->c + c->e, xb.height ) - xb.scissorRect.offset.y );
		break;
	case XOP_TRANSFORM:
		xb.transform = (qboolean)( c->a != 0 );
		memcpy( xb.m, c->m, sizeof( xb.m ) );
		break;
	case XOP_CLIP_ENABLE:
		xb.clip = (qboolean)( c->a != 0 );
		break;
	case XOP_CLIP_RENDER:
		XB_ClipRender( c );
		break;
	case XOP_PUSH:
		XB_Push();
		break;
	case XOP_POP:
		if ( xb.refused > 0 )
			xb.refused--;
		else if ( xb.top > 0 ) {
			XenUI_EndPass();
			xb.top--;
		}
		break;
	case XOP_COMPOSITE:
		XB_Composite( c );
		break;
	case XOP_SAVE_TEXTURE:
		XB_SaveTexture( c );
		break;
	case XOP_SAVE_MASK:
		XB_SaveMask();
		break;
	case XOP_END:
		{
			VkMemoryBarrier	mb;
			color4ub_t		white = { 255, 255, 255, 255 };

			shader_t *out = xb.outShader[xb.pass];

			XenUI_EndPass();	// resolves the base layer into outImage
			memset( &mb, 0, sizeof( mb ) );
			mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
			mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			qvkCmdPipelineBarrier( XB(), VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL );
			xb.passOpen = qfalse;
			xb.pass++;

			// the base layer over the frame, as a 2D picture
			if ( out != tess.shader ) {
				RB_EndSurface();
				backEnd.currentEntity = &backEnd.entity2D;
				vk_set_2d();
				RB_BeginSurface( out, 0, 0 );
			}
#ifdef USE_VBO
			VBO_UnBind();
#endif
			vk_set_2d();
			if ( vk.bloomActive )
				vk_bloom();
			RB_AddQuadStamp2( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 1, 1, white );
		}
		break;
	}
	return (const void *)( c + 1 );
}

// xenui's command buffer, ahead of the frame's (vk_end_frame, after vk_upload_flush)
void R_XenUISubmit( void )
{
	VkSubmitInfo si;

	if ( !xb.recording || xb.recFrame != tr.frameCount )
		return;
	if ( xb.passOpen ) {	// a pass never ended: nothing draws it, and the next pass of this frame would clear it
		XenUI_EndPass();
		xb.passOpen = qfalse;
	}
	if ( xb.queries ) {
		xb.writeTimestamp( XB(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, xb.queries, 2 * xb.slot + 1 );
		xb.timed[xb.slot] = qtrue;
	}
	VK_CHECK( qvkEndCommandBuffer( XB() ) );
	xb.recording = qfalse;
	memset( &si, 0, sizeof( si ) );
	si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &xb.slots[xb.slot].cmd;
	VK_CHECK( qvkQueueSubmit( vk.queue, 1, &si, xb.slots[xb.slot].fence ) );
	xb.slots[xb.slot].submitted = qtrue;
}
