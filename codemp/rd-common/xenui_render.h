#pragma once

// xenui's renderer interface (docs/wishlist/ui-system.md section 4): what the UI module's RmlUi render interface needs
// from the renderer, one to one with Rml::RenderInterface: the required calls plus scissor and transform (version 1),
// then clip masks, layers, filters and shaders (version 2).
//
// A renderer that has it exports XenUI_GetRenderAPI and sets the read-only cvar r_xenUIRenderApi to the version. The
// UI module finds the export in the loaded renderer module itself (xenui.cpp), because the installed engine's UI
// import table has no slot for it yet; when the engine gains one, the same struct moves into uiImport_t.ext.
// XenUI_GetRenderAPI( version ) returns the struct for any version up to its own; later versions only append.
//
// Coordinates are window pixels (glConfig.vidWidth x vidHeight), origin top left. Colours are premultiplied alpha, and
// textures are drawn premultiplied (ONE, ONE_MINUS_SRC_ALPHA).

#define XENUI_RENDER_API_VERSION	2

typedef struct xenUIVertex_s {
	float			xy[2];
	unsigned char	rgba[4];	// premultiplied
	float			st[2];
} xenUIVertex_t;	// the layout of Rml::Vertex

// clip mask operations (Rml::ClipMaskOperation) and blend modes (Rml::BlendMode)
enum { XENUI_CLIP_SET, XENUI_CLIP_SET_INVERSE, XENUI_CLIP_INTERSECT };
enum { XENUI_BLEND, XENUI_BLEND_REPLACE };

// a filter (RmlUi's basic filters arrive as a colour matrix or an opacity)
enum { XENUI_FILTER_OPACITY, XENUI_FILTER_BLUR, XENUI_FILTER_DROP_SHADOW, XENUI_FILTER_COLOR_MATRIX };
typedef struct xenUIFilter_s {
	int		type;
	float	value;			// opacity
	float	sigma;			// blur, drop shadow: pixels
	float	offset[2];		// drop shadow: pixels
	float	color[4];		// drop shadow: premultiplied
	float	matrix[16];		// colour matrix, column major, on the premultiplied colour
} xenUIFilter_t;

// a gradient shader: the geometry's texture coordinates are positions in the element's box
enum { XENUI_SHADER_LINEAR, XENUI_SHADER_RADIAL, XENUI_SHADER_CONIC };
#define XENUI_MAX_STOPS	16
typedef struct xenUIShader_s {
	int		type;
	int		repeating;
	int		numStops;
	float	p[2];			// linear: start; radial and conic: centre
	float	v[2];			// linear: end - start; radial: 1 / radius per axis; conic: cos and sin of the start angle
	float	stopColors[XENUI_MAX_STOPS][4];	// premultiplied
	float	stopPositions[XENUI_MAX_STOPS];
} xenUIShader_t;

typedef struct xenUIRenderAPI_s {
	int		version;	// XENUI_RENDER_API_VERSION
	int		size;		// sizeof( xenUIRenderAPI_t ): later versions only append

	// geometry is copied into renderer memory; a release takes effect after this frame's draws
	int		(*CompileGeometry)( const xenUIVertex_t *verts, int numVerts, const int *indexes, int numIndexes );
	void	(*ReleaseGeometry)( int geometry );
	void	(*RenderGeometry)( int geometry, float tx, float ty, int texture );	// texture 0 = untextured

	// textures are premultiplied RGBA; LoadTexture premultiplies a file (any format the renderer loads)
	int		(*CreateTexture)( const unsigned char *rgba, int width, int height );
	int		(*LoadTexture)( const char *path, int *width, int *height );
	void	(*ReleaseTexture)( int texture );

	// state for the following RenderGeometry calls (window pixels; the transform is column major, NULL = identity)
	void	(*SetScissor)( int enable, int x, int y, int width, int height );
	void	(*SetTransform)( const float *matrix );

	void	(*GetWindowSize)( int *width, int *height );

	// --- version 2: RmlUi's optional render interface ---------------------------------------------------------------
	// Everything draws into the renderer's xenui layers, drawn over the frame at EndFrame (call it after RmlUi's Render
	// each frame anything was drawn). Layer 0 is the base; PushLayer returns the new top layer. A frame takes two
	// passes (Render then EndFrame, twice): each draws where its EndFrame falls among the frame's 2D and 3D drawing, so
	// a document can sit under a 3D view the UI draws later.
	void	(*EnableClipMask)( int enable );
	void	(*RenderToClipMask)( int operation, int geometry, float tx, float ty );
	int		(*PushLayer)( void );
	void	(*CompositeLayers)( int source, int destination, int blendMode, const int *filters, int numFilters );
	void	(*PopLayer)( void );
	int		(*SaveLayerAsTexture)( void );		// the top layer inside the scissor region, as a texture of that size
	int		(*SaveLayerAsMaskImage)( void );	// the top layer as a mask, returned as a filter
	int		(*CompileFilter)( const xenUIFilter_t *filter );
	void	(*ReleaseFilter)( int filter );
	int		(*CompileShader)( const xenUIShader_t *shader );
	void	(*RenderShader)( int shader, int geometry, float tx, float ty, int texture );
	void	(*ReleaseShader)( int shader );
	void	(*EndFrame)( void );
	float	(*GPUTime)( void );					// milliseconds of the last frame's xenui drawing on the GPU (0: unknown)
} xenUIRenderAPI_t;

#define XENUI_RENDER_API_V1_SIZE	( (int)( (size_t)&( (xenUIRenderAPI_t *)0 )->EnableClipMask ) )

typedef const xenUIRenderAPI_t *( *xenUIGetRenderAPI_t )( int version );
#define XENUI_RENDER_API_EXPORT	"XenUI_GetRenderAPI"
