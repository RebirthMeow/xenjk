/*
===========================================================================
xenJK -- Ogg Theora cinematic playback backend.

Drop-in alternative to the legacy RoQ decoder in cl_cin.cpp. Hooks into
the same five public CIN_* entry points via a backend dispatcher in
cl_cin.cpp. Compiled in only when -DUSE_THEORA=1 (set by CMake when the
top-level BuildTheora option is ON).

Threading model:
  - The TheoraPlay worker thread runs entirely in isolation. It reads
    from a memory-backed I/O callback over the handle's one copy of the
    .ogv, read into Z_Malloc'd memory at Open time so the worker never
    touches JKA's non-thread-safe FS_*. The reader does not own that copy:
    loops start a new reader over it and Stop frees it after the join.
  - The engine main thread calls Run / Draw / Upload, which drain
    TheoraPlay's thread-safe queues for the latest video frame and any
    pending audio packets.
  - On Stop, THEORAPLAY_stopDecode() joins the worker before we tear
    down state, so there is no possibility of a worker-thread access
    after free.

License: same as OpenJK (GPLv2-or-later). TheoraPlay itself is zlib
licensed and the underlying Ogg/Vorbis/Theora libraries are BSD; all
GPL-compatible.
===========================================================================
*/

#include "client.h"

#ifdef USE_THEORA

#include "theoraplay.h"

// =====================================================================
//  per-handle state
// =====================================================================

#define THEORA_MAX_HANDLES   16    // cl_cin.cpp's MAX_VIDEO_HANDLES (it range-checks every handle first)

void CIN_THEORA_Stop( int handle );

typedef struct theoraVideo_s {
	qboolean                inUse;

	// rendering rect (640x480 virtual coords, set via CIN_THEORA_SetExtents)
	int                     x, y, w, h;
	int                     systemBits;

	// TheoraPlay decode state
	THEORAPLAY_Decoder     *decoder;
	THEORAPLAY_Io          *io;          // ownership held by TheoraPlay; it closes the reader (not the file)

	// most recent decoded frame -- we hold one ahead so Draw/Upload can
	// repaint it on each frame even when no new frame has arrived yet.
	const THEORAPLAY_VideoFrame *currentFrame;

	// single-frame look-ahead: a popped frame whose playms is in the
	// future. Held here until its time arrives, at which point it gets
	// promoted to currentFrame. Without this we'd burn through the queue
	// at the rate Run is invoked (multiple times per render frame for
	// videoMap shaders), causing 2-3x apparent playback speed.
	const THEORAPLAY_VideoFrame *pendingFrame;

	int                     videoWidth;
	int                     videoHeight;

	int                     startTimeMs;  // cls.realtime when playback began
	int                     lastRunMs;    // last Run() wall-clock time (de-dup multi-surface re-entry)
	qboolean                done;          // end-of-stream reached
	qboolean                gotFirstFrame;
	qboolean                frameDirty;    // currentFrame not yet uploaded (upload once per new frame)
	qboolean                stillOnWalls;  // cl_inGameVideo 0: a non-system video shows its first frame only

	// playback-mode flags (set in Open, derived from systemBits)
	qboolean                looping;       // CIN_loop  -- rewind on EOF
	qboolean                silent;        // CIN_silent -- skip audio pump
	qboolean                holdAtEnd;     // CIN_hold  -- keep last frame on EOF

	// the whole file, read once at Open; every reader (one per loop) reads it
	byte                   *fileBuffer;
	long                    fileBufferSize;
} theoraVideo_t;

static theoraVideo_t cinTheora[THEORA_MAX_HANDLES];

// =====================================================================
//  memory-backed I/O (so the worker thread never calls FS_*)
// =====================================================================

typedef struct memReader_s {
	byte *data;
	long  size;
	long  pos;
} memReader_t;

static long memIO_read( THEORAPLAY_Io *io, void *buf, long buflen ) {
	memReader_t *m = (memReader_t *)io->userdata;
	long avail = m->size - m->pos;
	if ( avail <= 0 ) return 0;
	if ( buflen > avail ) buflen = avail;
	Com_Memcpy( buf, m->data + m->pos, buflen );
	m->pos += buflen;
	return buflen;
}
static long memIO_streamlen( THEORAPLAY_Io *io ) {
	return ((memReader_t *)io->userdata)->size;
}
static int memIO_seek( THEORAPLAY_Io *io, long pos ) {
	memReader_t *m = (memReader_t *)io->userdata;
	if ( pos < 0 || pos > m->size ) return -1;
	m->pos = pos;
	return 0;
}
// Frees the reader, never the data: the file stays with the handle.
static void memIO_close( THEORAPLAY_Io *io ) {
	if ( io && io->userdata ) {
		Z_Free( io->userdata );
		io->userdata = NULL;
	}
	if ( io ) Z_Free( io );
}

// A decoder over the handle's file. THEORAPLAY_startDecode closes the
// reader itself when it fails, so the caller must not close it again
// (the old caller double-freed it).
static THEORAPLAY_Decoder *TheoraStart( theoraVideo_t *v ) {
	memReader_t   *mem;
	THEORAPLAY_Io *io;
	THEORAPLAY_Decoder *decoder;

	mem = (memReader_t *)Z_Malloc( sizeof( *mem ), TAG_FILESYS, qtrue );
	mem->data = v->fileBuffer;
	mem->size = v->fileBufferSize;
	mem->pos  = 0;
	io = (THEORAPLAY_Io *)Z_Malloc( sizeof( *io ), TAG_FILESYS, qtrue );
	io->read      = memIO_read;
	io->streamlen = memIO_streamlen;
	io->seek      = memIO_seek;
	io->close     = memIO_close;
	io->userdata  = mem;

	// 30-frame look-ahead, RGBA output, allocator=NULL (TheoraPlay's own
	// malloc; frames are freed with freeVideo); multithreaded=1 runs the
	// worker thread.
	decoder = THEORAPLAY_startDecode( io, 30, THEORAPLAY_VIDFMT_RGBA, NULL, 1 );
	v->io = decoder ? io : NULL;
	return decoder;
}

// =====================================================================
//  audio plumbing -- convert Vorbis float32 to S16 and pump S_RawSamples
// =====================================================================

static void TheoraAudio_Push( const THEORAPLAY_AudioPacket *packet ) {
	short  out[8192 * 2];
	int    total;
	int    i;
	float  s;

	if ( packet->channels < 1 || packet->channels > 2 ) return;
	total = packet->frames * packet->channels;
	if ( total <= 0 || total > (int)( sizeof(out) / sizeof(out[0]) ) ) return;

	for ( i = 0; i < total; i++ ) {
		s = packet->samples[i];
		if ( s >  1.0f ) s =  1.0f;
		if ( s < -1.0f ) s = -1.0f;
		out[i] = (short)( s * 32767.0f );
	}

	// width=2 (16-bit), volume 1.0, bFirstOrOnly=1
	S_RawSamples( packet->frames, packet->freq, 2, packet->channels,
				  (byte *)out, 1.0f, 1 );
}

// =====================================================================
//  public: Open
// =====================================================================

qboolean CIN_THEORA_Open( int handle, const char *filename, int x, int y, int w, int h, int systemBits ) {
	theoraVideo_t  *v;
	fileHandle_t    fh;
	long            len;

	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) {
		return qfalse;
	}
	v = &cinTheora[handle];
	if ( v->inUse ) {
		// recycle a stale handle slot
		Com_Printf( "CIN_THEORA_Open: handle %d already in use, stopping prior\n", handle );
		CIN_THEORA_Stop( handle );
	}
	Com_Memset( v, 0, sizeof( *v ) );

	// load whole file into memory so the worker thread can read without
	// touching JKA's non-thread-safe FS layer
	len = FS_FOpenFileRead( filename, &fh, qtrue );
	if ( len <= 0 || !fh ) {
		Com_Printf( "CIN_THEORA_Open: cannot open %s\n", filename );
		return qfalse;
	}
	v->fileBuffer     = (byte *)Z_Malloc( len, TAG_FILESYS, qfalse );
	v->fileBufferSize = len;
	FS_Read( v->fileBuffer, len, fh );
	FS_FCloseFile( fh );

	v->decoder = TheoraStart( v );
	if ( !v->decoder ) {
		Com_Printf( "CIN_THEORA_Open: TheoraPlay failed to start on %s\n", filename );
		Z_Free( v->fileBuffer );
		v->fileBuffer = NULL;
		v->fileBufferSize = 0;
		return qfalse;
	}

	v->x            = x;
	v->y            = y;
	v->w            = w;
	v->h            = h;
	v->systemBits   = systemBits;
	v->startTimeMs  = 0;  // lazy: anchored on first frame delivery in Run()
	v->lastRunMs    = 0;
	v->inUse        = qtrue;
	v->done         = qfalse;
	v->gotFirstFrame= qfalse;
	v->looping      = (systemBits & CIN_loop)   ? qtrue : qfalse;
	v->silent       = (systemBits & CIN_silent) ? qtrue : qfalse;
	v->holdAtEnd    = (systemBits & CIN_hold)   ? qtrue : qfalse;
	v->stillOnWalls = ( !(systemBits & CIN_system) && !cl_inGameVideo->integer ) ? qtrue : qfalse;

	return qtrue;
}

// =====================================================================
//  public: Run -- drain audio + advance video to current play time
// =====================================================================

e_status CIN_THEORA_Run( int handle ) {
	theoraVideo_t *v;
	int playMs;

	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) return FMV_EOF;
	v = &cinTheora[handle];
	if ( !v->inUse || !v->decoder ) return FMV_EOF;
	if ( v->done ) return FMV_EOF;
	if ( v->stillOnWalls && v->gotFirstFrame ) return FMV_PLAY;	// cl_inGameVideo 0, as RoQ's playonwalls

	// Defensive de-duplication: if a videoMap shader is on multiple
	// surfaces in the world, the renderer calls R_BindAnimatedImage
	// (which calls us) once per surface per frame. We do all the work
	// on the first call of the frame and short-circuit subsequent ones
	// (still allow pending->current promotion in case wall clock moved
	// significantly between invocations).
	{
		int nowMs = (int)( Sys_Milliseconds() * com_timescale->value );
		if ( v->lastRunMs && (nowMs - v->lastRunMs) < 4 ) {
			if ( v->pendingFrame && v->startTimeMs &&
				 (int)v->pendingFrame->playms <= nowMs - v->startTimeMs ) {
				if ( v->currentFrame ) THEORAPLAY_freeVideo( v->currentFrame );
				v->currentFrame = v->pendingFrame; v->frameDirty = qtrue;
				v->videoWidth   = v->pendingFrame->width;
				v->videoHeight  = v->pendingFrame->height;
				v->pendingFrame = NULL;
			}
			return v->gotFirstFrame ? FMV_PLAY : FMV_LOOPED;
		}
		v->lastRunMs = nowMs;
	}

	// Throttle audio so we don't overrun the engine's circular buffer.
	// MAX_RAW_SAMPLES is 16384 (~370 ms at 44.1 kHz stereo). We push as
	// long as the engine queue has less than ~half a second buffered;
	// any extra audio stays in TheoraPlay's queue (it has its own
	// look-ahead, so the worker thread will idle until we drain).
	// CIN_silent (videoMap shaders, menu loops) -- still drain the
	// queue so the worker doesn't stall, but skip pushing to the mixer.
	while ( !v->silent && THEORAPLAY_availableAudio( v->decoder ) ) {
		const THEORAPLAY_AudioPacket *audio;
		extern int s_rawend, s_soundtime;
		if ( s_rawend - s_soundtime > 8192 ) {
			// engine queue has plenty buffered; let it drain first
			break;
		}
		audio = THEORAPLAY_getAudio( v->decoder );
		if ( !audio ) break;
		TheoraAudio_Push( audio );
		THEORAPLAY_freeAudio( audio );
	}
	if ( v->silent ) {
		while ( THEORAPLAY_availableAudio( v->decoder ) ) {
			const THEORAPLAY_AudioPacket *audio = THEORAPLAY_getAudio( v->decoder );
			if ( !audio ) break;
			THEORAPLAY_freeAudio( audio );
		}
	}

	// Use Sys_Milliseconds (modulated by com_timescale) like the RoQ
	// path does -- cls.realtime can drift in CA_CINEMATIC state.
	// We anchor startTimeMs to the moment of FIRST frame delivery, not
	// to Open() time, otherwise the worker thread's pre-decoded
	// look-ahead would already be 'in the past' on the first Run() and
	// we'd skip the opening of the video.
	if ( v->startTimeMs == 0 ) {
		playMs = 0;
	} else {
		playMs = (int)( Sys_Milliseconds() * com_timescale->value ) - v->startTimeMs;
	}

	// First, if a previously-held-back pending frame is now due, promote it.
	if ( v->pendingFrame && (int)v->pendingFrame->playms <= playMs ) {
		if ( v->currentFrame ) THEORAPLAY_freeVideo( v->currentFrame );
		v->currentFrame  = v->pendingFrame; v->frameDirty = qtrue;
		v->videoWidth    = v->pendingFrame->width;
		v->videoHeight   = v->pendingFrame->height;
		v->pendingFrame  = NULL;
		if ( !v->gotFirstFrame ) {
			v->startTimeMs   = (int)( Sys_Milliseconds() * com_timescale->value ) - (int)v->currentFrame->playms;
			v->gotFirstFrame = qtrue;
		}
	}

	// Advance video to the latest frame whose playms <= playMs. The
	// FIRST future frame we encounter goes into pendingFrame and we
	// stop pulling -- this is critical, because Run() can be invoked
	// multiple times per render frame (videoMap shaders), and we must
	// not promote future frames just because we popped them.
	if ( !v->pendingFrame ) {
		while ( THEORAPLAY_availableVideo( v->decoder ) ) {
			const THEORAPLAY_VideoFrame *vf = THEORAPLAY_getVideo( v->decoder );
			if ( !vf ) break;

			// FIRST FRAME EVER: anchor the wall clock to this frame's playms
			// and make it current immediately, so subsequent comparisons have
			// a meaningful playMs to test against. Without this special case
			// the first frame would always look 'future' relative to
			// playMs=0 and end up parked in pendingFrame forever (deadlock
			// since pendingFrame is only promoted when playMs catches up).
			if ( !v->gotFirstFrame ) {
				if ( v->currentFrame ) THEORAPLAY_freeVideo( v->currentFrame );
				v->currentFrame  = vf; v->frameDirty = qtrue;
				v->videoWidth    = vf->width;
				v->videoHeight   = vf->height;
				v->startTimeMs   = (int)( Sys_Milliseconds() * com_timescale->value ) - (int)vf->playms;
				v->gotFirstFrame = qtrue;
				playMs           = (int)vf->playms; // re-anchor for subsequent iterations
				continue;
			}

			if ( (int)vf->playms > playMs ) {
				// future -- park it, promote on a later Run when it's due
				v->pendingFrame = vf;
				break;
			}

			// current or stale -- swap into currentFrame and keep walking
			// (so the *latest* on-time frame wins)
			if ( v->currentFrame ) THEORAPLAY_freeVideo( v->currentFrame );
			v->currentFrame = vf; v->frameDirty = qtrue;
			v->videoWidth   = vf->width;
			v->videoHeight  = vf->height;
		}
	}

	// end-of-stream is detected when the decoder is no longer decoding AND
	// we've drained both queues
	if ( !THEORAPLAY_isDecoding( v->decoder )
		 && !THEORAPLAY_availableVideo( v->decoder )
		 && !THEORAPLAY_availableAudio( v->decoder ) ) {

		// CIN_loop: rebuild the decoder from our private file copy and
		// re-anchor the clock on the next first frame.
		if ( v->looping && v->fileBuffer ) {
			THEORAPLAY_stopDecode( v->decoder );
			v->decoder = NULL;
			v->io      = NULL;
			if ( v->currentFrame ) { THEORAPLAY_freeVideo( v->currentFrame ); v->currentFrame = NULL; }
			if ( v->pendingFrame ) { THEORAPLAY_freeVideo( v->pendingFrame ); v->pendingFrame = NULL; }

			v->decoder = TheoraStart( v );
			if ( v->decoder ) {
				v->startTimeMs   = 0;
				v->lastRunMs     = 0;
				v->gotFirstFrame = qfalse;
				return FMV_LOOPED;
			}
			v->done = qtrue;
			return FMV_EOF;
		}

		// CIN_hold: keep the last frame visible (Draw/Upload still paint
		// the cached currentFrame). Just stop advancing.
		if ( v->holdAtEnd ) {
			return FMV_PLAY;
		}

		v->done = qtrue;
		return FMV_EOF;
	}

	if ( THEORAPLAY_decodingError( v->decoder ) ) {
		Com_Printf( "CIN_THEORA_Run: decode error on handle %d\n", handle );
		v->done = qtrue;
		return FMV_EOF;
	}

	return v->gotFirstFrame ? FMV_PLAY : FMV_LOOPED;
}

// =====================================================================
//  public: Draw / Upload
// =====================================================================

void CIN_THEORA_Upload( int handle ) {
	theoraVideo_t *v;

	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) return;
	v = &cinTheora[handle];
	if ( !v->inUse || !v->gotFirstFrame || !v->currentFrame ) return;

	// Called once per surface per frame: only a new frame is uploaded, the
	// rest just bind it (every bind re-uploaded).
	re->UploadCinematic( v->videoWidth, v->videoHeight,
		v->currentFrame->pixels, handle, v->frameDirty );
	v->frameDirty = qfalse;
}

void CIN_THEORA_Draw( int handle ) {
	theoraVideo_t *v;

	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) return;
	v = &cinTheora[handle];
	if ( !v->inUse || !v->gotFirstFrame || !v->currentFrame ) return;

	// DrawStretchRaw uploads when dirty itself
	re->DrawStretchRaw( v->x, v->y, v->w, v->h, v->videoWidth, v->videoHeight,
		v->currentFrame->pixels, handle, v->frameDirty );
	v->frameDirty = qfalse;
}

// =====================================================================
//  public: SetExtents / Stop
// =====================================================================

void CIN_THEORA_SetExtents( int handle, int x, int y, int w, int h ) {
	theoraVideo_t *v;
	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) return;
	v = &cinTheora[handle];
	if ( !v->inUse ) return;
	v->x = x;
	v->y = y;
	v->w = w;
	v->h = h;
}

void CIN_THEORA_Stop( int handle ) {
	theoraVideo_t *v;
	if ( handle < 0 || handle >= THEORA_MAX_HANDLES ) return;
	v = &cinTheora[handle];
	if ( !v->inUse ) return;

	if ( v->currentFrame ) {
		THEORAPLAY_freeVideo( v->currentFrame );
		v->currentFrame = NULL;
	}
	if ( v->pendingFrame ) {
		THEORAPLAY_freeVideo( v->pendingFrame );
		v->pendingFrame = NULL;
	}
	if ( v->decoder ) {
		THEORAPLAY_stopDecode( v->decoder );  // joins worker thread
		v->decoder = NULL;
	}
	// THEORAPLAY_stopDecode joined the worker and closed the reader; the
	// file it read is freed here, after the join.
	v->io = NULL;
	if ( v->fileBuffer ) {
		Z_Free( v->fileBuffer );
		v->fileBuffer = NULL;
		v->fileBufferSize = 0;
	}
	v->inUse = qfalse;
	v->done  = qtrue;
}

// =====================================================================
//  public: format-detection helper used by cl_cin.cpp dispatch
// =====================================================================

qboolean CIN_THEORA_IsTheoraFile( const char *filename ) {
	const char *ext;
	int len;
	if ( !filename || !*filename ) return qfalse;
	len = (int)strlen( filename );
	if ( len >= 4 ) {
		ext = filename + len - 4;
		if ( !Q_stricmp( ext, ".ogv" ) ) return qtrue;
		if ( !Q_stricmp( ext, ".ogm" ) ) return qtrue;
	}

	// Magic-byte fallback: any file starting with 'OggS' is an Ogg
	// container -- almost certainly Theora video for our purposes.
	// This lets users drop an .ogv next to a shader expecting .roq
	// (e.g., the menu's gfx/menus/videologo) without editing shaders.
	{
		fileHandle_t fh;
		long         flen;
		byte         magic[4] = { 0, 0, 0, 0 };
		flen = FS_FOpenFileRead( filename, &fh, qtrue );
		if ( flen >= 4 && fh ) {
			FS_Read( magic, 4, fh );
			FS_FCloseFile( fh );
			if ( magic[0] == 'O' && magic[1] == 'g' && magic[2] == 'g' && magic[3] == 'S' ) {
				return qtrue;
			}
		} else if ( fh ) {
			FS_FCloseFile( fh );
		}
	}

	return qfalse;
}

#endif /* USE_THEORA */
