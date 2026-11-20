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
Video capture: ffmpeg sink for the video command.

cl_avi.cpp still owns the recording session (frame clock, capture buffers, the renderer's TakeVideoFrame); with
cl_videoCodec other than "avi" its frames and audio come here instead of into an AVI file. Frames go to an ffmpeg
child process as raw BGR24 (the renderer's uncompressed AVI layout: bottom-up rows padded to 4 bytes) through a pipe,
written by a worker thread from a small queue, so the game thread only copies each frame once. ffmpeg flips,
converts to BT.709 YUV (tagged) and encodes with a GPU encoder when cl_videoGPU allows and ffmpeg has one, else a
software one. Audio is kept as raw PCM next to the video and muxed in when the recording stops.

Codecs (cl_videoCodec): h264 / hevc / av1 (.mp4, AAC), ffv1 (lossless RGB .mkv, FLAC), prores (ProRes 422 HQ .mov,
PCM). cl_videoQuality is the constant-quality level (CRF/CQ, lower is better). cl_videoArgs replaces the encoder
options when set. ffmpeg is found from cl_videoFFmpeg, then next to the game, then on PATH.
*/

#include "client.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#endif

cvar_t *cl_videoCodec;
cvar_t *cl_videoQuality;
cvar_t *cl_videoGPU;
cvar_t *cl_videoFFmpeg;
cvar_t *cl_videoArgs;
cvar_t *cl_videoAudioVolume;
cvar_t *cl_videoNotify;

typedef struct {
	const char	*name;
	const char	*ext;
	const char	*software;
	const char	*gpu[3];		// tried in order when cl_videoGPU is set, ffmpeg lists them and a test encode works
	const char	*audioArgs;
} videoCodec_t;

static const videoCodec_t videoCodecs[] = {
	{ "h264",	"mp4",	"libx264",		{ "h264_nvenc", "h264_qsv", "h264_amf" },	"-c:a aac -b:a 320k" },
	{ "hevc",	"mp4",	"libx265",		{ "hevc_nvenc", "hevc_qsv", "hevc_amf" },	"-c:a aac -b:a 320k" },
	{ "av1",	"mp4",	"libsvtav1",	{ "av1_nvenc", "av1_qsv", "av1_amf" },		"-c:a aac -b:a 320k" },
	{ "ffv1",	"mkv",	"ffv1",			{ NULL, NULL, NULL },						"-c:a flac" },
	{ "prores",	"mov",	"prores_ks",	{ NULL, NULL, NULL },						"-c:a pcm_s16le" },
};

#define VIDEO_QUEUE_FRAMES 3

static struct {
	qboolean			active;
	qboolean			audio;
	int					width, height, fps, rate, channels;
	int					frames;
	long long			audioBytes;
	char				ffmpeg[MAX_OSPATH];
	char				encoder[64];
	char				ext[8];
	const char			*audioArgs;
	char				name[MAX_OSPATH];		// videos/<name> without extension
	char				videoPath[MAX_OSPATH];	// what ffmpeg writes (a temp name when audio is muxed in later)
	fileHandle_t		pcm;

#ifdef _WIN32
	HANDLE				process;
	HANDLE				pipe;
#else
	FILE				*pipe;
#endif

	// writer thread: the game thread fills a free buffer, the worker writes full ones to the pipe in order
	std::thread			*worker;
	std::mutex			lock;
	std::condition_variable	cond;
	std::vector<byte>	buffers[VIDEO_QUEUE_FRAMES];
	int					head, count;			// queue of full buffers (indices into buffers)
	bool				stop;
	bool				failed;
} vp;

static std::string videoEncoderList;		// `ffmpeg -encoders`, read once per ffmpeg path
static std::string videoEncoderListPath;
static std::string videoEncoderProbed;		// "+enc\n" a test encode worked, "-enc\n" it failed, for that ffmpeg

void CL_VideoPipe_Init( void )
{
	cl_videoCodec = Cvar_Get( "cl_videoCodec", "h264", CVAR_ARCHIVE_ND,
		"video command output: h264, hevc, av1 (.mp4), ffv1 (lossless .mkv), prores (.mov) through ffmpeg, or avi (built in)" );
	cl_videoQuality = Cvar_Get( "cl_videoQuality", "18", CVAR_ARCHIVE_ND, "video constant quality (CRF/CQ); lower is better" );
	cl_videoGPU = Cvar_Get( "cl_videoGPU", "1", CVAR_ARCHIVE_ND, "video uses a GPU encoder (NVENC, Quick Sync, AMF) when ffmpeg has one" );
	cl_videoFFmpeg = Cvar_Get( "cl_videoFFmpeg", "", CVAR_ARCHIVE_ND | CVAR_PROTECTED, "path to ffmpeg; empty looks next to the game, then on PATH" );
	cl_videoArgs = Cvar_Get( "cl_videoArgs", "", CVAR_ARCHIVE_ND | CVAR_PROTECTED, "ffmpeg output options replacing the codec's own (advanced)" );
	cl_videoNotify = Cvar_Get( "cl_videoNotify", "0", CVAR_ARCHIVE_ND, "console notify lines are drawn into video recordings" );
	cl_videoAudioVolume = Cvar_Get( "cl_videoAudioVolume", "1", CVAR_ARCHIVE_ND, "recorded audio level, independent of s_volume (speakers silenced by s_mute still record)" );
}

static const videoCodec_t *CL_VideoCodec( void )
{
	for ( size_t i = 0; i < ARRAY_LEN( videoCodecs ); i++ ) {
		if ( !Q_stricmp( cl_videoCodec->string, videoCodecs[i].name ) )
			return &videoCodecs[i];
	}
	return NULL;
}

qboolean CL_VideoPipe_Wanted( void )
{
	return CL_VideoCodec() ? qtrue : qfalse;
}

const char *CL_VideoPipe_Extension( void )
{
	const videoCodec_t *codec = CL_VideoCodec();
	return codec ? codec->ext : "avi";
}

/*
===============
Process helpers
===============
*/
#ifdef _WIN32
// a path as one command-line argument (Windows file names cannot contain '"')
static std::string VP_Quote( const char *path )
{
	return std::string( "\"" ) + path + "\"";
}

static qboolean VP_FileExists( const char *path )
{
	DWORD attr = GetFileAttributesA( path );
	return ( attr != INVALID_FILE_ATTRIBUTES && !( attr & FILE_ATTRIBUTE_DIRECTORY ) ) ? qtrue : qfalse;
}

// runs a command line with stdin from a new pipe (stdinWrite) or none, stdout+stderr to logPath or captured
static qboolean VP_Spawn( const char *cmdline, HANDLE *stdinWrite, const char *logPath, std::string *capture, HANDLE *process )
{
	SECURITY_ATTRIBUTES sa = { sizeof( sa ), NULL, TRUE };
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	HANDLE inRead = NULL, inWrite = NULL, outRead = NULL, outWrite = NULL;
	std::vector<char> cmd( cmdline, cmdline + strlen( cmdline ) + 1 );
	BOOL ok;

	ZeroMemory( &si, sizeof( si ) );
	si.cb = sizeof( si );
	si.dwFlags = STARTF_USESTDHANDLES;

	if ( stdinWrite ) {
		if ( !CreatePipe( &inRead, &inWrite, &sa, 0 ) )
			return qfalse;
		SetHandleInformation( inWrite, HANDLE_FLAG_INHERIT, 0 );
		si.hStdInput = inRead;
	}
	if ( capture ) {
		if ( !CreatePipe( &outRead, &outWrite, &sa, 0 ) ) {
			if ( inRead ) { CloseHandle( inRead ); CloseHandle( inWrite ); }
			return qfalse;
		}
		SetHandleInformation( outRead, HANDLE_FLAG_INHERIT, 0 );
	} else if ( logPath ) {
		outWrite = CreateFileA( logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL );
		if ( outWrite == INVALID_HANDLE_VALUE )
			outWrite = NULL;
	}
	si.hStdOutput = outWrite;
	si.hStdError = outWrite;

	ok = CreateProcessA( NULL, cmd.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi );

	if ( inRead ) CloseHandle( inRead );
	if ( outWrite ) CloseHandle( outWrite );
	if ( !ok ) {
		if ( inWrite ) CloseHandle( inWrite );
		if ( outRead ) CloseHandle( outRead );
		return qfalse;
	}
	CloseHandle( pi.hThread );

	if ( capture ) {
		char buf[4096];
		DWORD got;
		while ( ReadFile( outRead, buf, sizeof( buf ), &got, NULL ) && got )
			capture->append( buf, got );
		CloseHandle( outRead );
	}
	if ( stdinWrite )
		*stdinWrite = inWrite;
	*process = pi.hProcess;
	return qtrue;
}

static int VP_Wait( HANDLE process )
{
	DWORD code = 1;
	WaitForSingleObject( process, INFINITE );
	GetExitCodeProcess( process, &code );
	CloseHandle( process );
	return (int)code;
}

static int VP_Run( const char *cmdline, const char *logPath, std::string *capture )
{
	HANDLE process;
	if ( !VP_Spawn( cmdline, NULL, logPath, capture, &process ) )
		return -1;
	return VP_Wait( process );
}
#else
// a path as one shell word: single-quoted, with each ' written as '\''
static std::string VP_Quote( const char *path )
{
	std::string out = "'";
	for ( const char *c = path; *c; c++ ) {
		if ( *c == '\'' )
			out += "'\\''";
		else
			out += *c;
	}
	return out + "'";
}

static qboolean VP_FileExists( const char *path )
{
	return access( path, X_OK ) == 0 ? qtrue : qfalse;
}

static int VP_Run( const char *cmdline, const char *logPath, std::string *capture )
{
	std::string cmd = cmdline;
	if ( capture ) {
		cmd += " 2>&1";
		FILE *f = popen( cmd.c_str(), "r" );
		if ( !f )
			return -1;
		char buf[4096];
		size_t got;
		while ( ( got = fread( buf, 1, sizeof( buf ), f ) ) > 0 )
			capture->append( buf, got );
		return pclose( f );
	}
	if ( logPath )
		cmd += " >>" + VP_Quote( logPath ) + " 2>&1";
	return system( cmd.c_str() );
}
#endif

static qboolean CL_VideoFindFFmpeg( char *out, int size )
{
#ifdef _WIN32
	const char *exe = "ffmpeg.exe";
#else
	const char *exe = "ffmpeg";
#endif
	const char *dirs[2] = { Sys_DefaultInstallPath(), Cvar_VariableString( "fs_basepath" ) };

	if ( cl_videoFFmpeg->string[0] ) {
		Q_strncpyz( out, cl_videoFFmpeg->string, size );
		return qtrue;
	}
	for ( int i = 0; i < 2; i++ ) {
		if ( !dirs[i] || !dirs[i][0] )
			continue;
		Com_sprintf( out, size, "%s%c%s", dirs[i], PATH_SEP, exe );
		if ( VP_FileExists( out ) )
			return qtrue;
	}
#ifdef _WIN32
	if ( SearchPathA( NULL, exe, NULL, size, out, NULL ) )
		return qtrue;
	return qfalse;
#else
	Q_strncpyz( out, exe, size );	// the shell searches PATH; the encoder probe fails if it is not there
	return qtrue;
#endif
}

static qboolean CL_VideoHasEncoder( const char *name )
{
	// lines look like " V....D h264_nvenc           NVIDIA NVENC H.264 encoder (codec h264)"
	std::string needle = std::string( " " ) + name + " ";
	return videoEncoderList.find( needle ) != std::string::npos ? qtrue : qfalse;
}

// the encoder's own options for constant quality q (cl_videoQuality), with its input pixel format
static std::string CL_VideoEncoderArgs( const char *enc, int q )
{
	char buf[256];

	if ( strstr( enc, "_nvenc" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v %s -preset p6 -tune hq -rc vbr -cq %d -b:v 0", enc, q );
	else if ( strstr( enc, "_qsv" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v %s -preset slower -global_quality %d", enc, q );
	else if ( strstr( enc, "_amf" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v %s -quality quality -rc cqp -qp_i %d -qp_p %d -qp_b %d", enc, q, q, q );
	else if ( !strcmp( enc, "libx264" ) || !strcmp( enc, "libx265" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v %s -preset medium -crf %d", enc, q );
	else if ( !strcmp( enc, "libsvtav1" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v libsvtav1 -preset 6 -crf %d", q + 8 );	// AV1's CRF scale runs to 63
	else if ( !strcmp( enc, "ffv1" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v ffv1 -level 3 -g 1 -slices 16 -slicecrc 1" );
	else if ( !strcmp( enc, "prores_ks" ) )
		Com_sprintf( buf, sizeof( buf ), "-c:v prores_ks -profile:v 3 -vendor apl0" );
	else
		Com_sprintf( buf, sizeof( buf ), "-c:v %s", enc );

	std::string args = buf;
	if ( !strcmp( enc, "libx265" ) || !strcmp( enc, "hevc_nvenc" ) || !strcmp( enc, "hevc_qsv" ) || !strcmp( enc, "hevc_amf" ) )
		args += " -tag:v hvc1";		// plays in Apple players
	return args;
}

static const char *CL_VideoPixelFormat( const char *enc )
{
	if ( !strcmp( enc, "ffv1" ) )
		return "bgr0";				// lossless: stays RGB
	if ( !strcmp( enc, "prores_ks" ) )
		return "yuv422p10le";
	if ( strstr( enc, "_qsv" ) )
		return "nv12";
	return "yuv420p";
}

// `ffmpeg -encoders` lists what ffmpeg was built with, not what this machine's GPU and driver can run: a one-frame
// encode to the null muxer says whether enc actually opens. Cached per ffmpeg path for the session.
static qboolean CL_VideoEncoderWorks( const char *enc )
{
	const std::string works = std::string( "+" ) + enc + "\n", fails = std::string( "-" ) + enc + "\n";
	std::string out;

	if ( videoEncoderProbed.find( works ) != std::string::npos )
		return qtrue;
	if ( videoEncoderProbed.find( fails ) != std::string::npos )
		return qfalse;

	std::string cmd = VP_Quote( vp.ffmpeg ) + " -hide_banner -nostdin -loglevel error -f lavfi -i color=c=black:s=256x256:r=30"
		" -frames:v 1 -an -vf format=" + CL_VideoPixelFormat( enc ) + " " + CL_VideoEncoderArgs( enc, cl_videoQuality->integer ) +
		" -f null -";
	const qboolean ok = VP_Run( cmd.c_str(), NULL, &out ) == 0 ? qtrue : qfalse;
	videoEncoderProbed += ok ? works : fails;
	if ( !ok )
		Com_Printf( "video: %s is listed by ffmpeg but does not run here\n", enc );
	return ok;
}

/*
===============
Writer thread
===============
*/
static void CL_VideoWriter( void )
{
#ifndef _WIN32
	// a write after ffmpeg exits fails with EPIPE (vp.failed) instead of raising SIGPIPE, which would kill the game.
	// The signal is thread-directed, so blocking it here covers every pipe write (the stream is unbuffered, so
	// pclose has nothing left to flush on the game thread).
	sigset_t pipeSignal;
	sigemptyset( &pipeSignal );
	sigaddset( &pipeSignal, SIGPIPE );
	pthread_sigmask( SIG_BLOCK, &pipeSignal, NULL );
#endif

	for ( ;; ) {
		int index;
		{
			std::unique_lock<std::mutex> l( vp.lock );
			vp.cond.wait( l, [] { return vp.count > 0 || vp.stop; } );
			if ( !vp.count )
				return;		// stop, queue drained
			index = vp.head;
		}

		const std::vector<byte> &frame = vp.buffers[index];
		bool ok = true;
#ifdef _WIN32
		const byte *p = frame.data();
		size_t left = frame.size();
		while ( left && ok ) {
			DWORD wrote = 0;
			ok = WriteFile( vp.pipe, p, (DWORD)( left > ( 1 << 24 ) ? ( 1 << 24 ) : left ), &wrote, NULL ) && wrote;
			p += wrote;
			left -= wrote;
		}
#else
		ok = fwrite( frame.data(), 1, frame.size(), vp.pipe ) == frame.size();
#endif
		{
			std::lock_guard<std::mutex> l( vp.lock );
			vp.head = ( vp.head + 1 ) % VIDEO_QUEUE_FRAMES;
			vp.count--;
			if ( !ok )
				vp.failed = true;
		}
		vp.cond.notify_all();
		if ( !ok ) {
			// ffmpeg went away: drop the rest so the game thread never blocks on a dead pipe
			std::lock_guard<std::mutex> l( vp.lock );
			vp.count = 0;
			return;
		}
	}
}

/*
===============
CL_VideoPipe_Open

name is the game path without extension (videos/<name>). Returns qfalse when ffmpeg or the encoder is missing so
the caller can fall back to AVI.
===============
*/
qboolean CL_VideoPipe_Open( const char *name, int width, int height, int fps, qboolean audio, int rate, int channels )
{
	const videoCodec_t *codec = CL_VideoCodec();
	const char *enc = NULL;
	char logPath[MAX_OSPATH], outPath[MAX_OSPATH];
	fileHandle_t f;

	if ( !codec || vp.active )
		return qfalse;

	if ( !CL_VideoFindFFmpeg( vp.ffmpeg, sizeof( vp.ffmpeg ) ) ) {
		Com_Printf( S_COLOR_YELLOW "video: ffmpeg not found (set cl_videoFFmpeg, or put ffmpeg next to the game); recording AVI\n" );
		return qfalse;
	}

	if ( videoEncoderListPath != vp.ffmpeg ) {
		videoEncoderList.clear();
		videoEncoderProbed.clear();
		if ( VP_Run( ( VP_Quote( vp.ffmpeg ) + " -hide_banner -encoders" ).c_str(), NULL, &videoEncoderList ) != 0 || videoEncoderList.empty() ) {
			Com_Printf( S_COLOR_YELLOW "video: could not run %s; recording AVI\n", vp.ffmpeg );
			videoEncoderList.clear();
			return qfalse;
		}
		videoEncoderListPath = vp.ffmpeg;
	}

	if ( cl_videoGPU->integer ) {
		for ( int i = 0; i < 3 && !enc; i++ ) {
			if ( codec->gpu[i] && CL_VideoHasEncoder( codec->gpu[i] ) && CL_VideoEncoderWorks( codec->gpu[i] ) )
				enc = codec->gpu[i];
		}
	}
	if ( !enc && CL_VideoHasEncoder( codec->software ) )
		enc = codec->software;
	if ( !enc ) {
		Com_Printf( S_COLOR_YELLOW "video: this ffmpeg has no %s encoder; recording AVI\n", codec->name );
		return qfalse;
	}

	// with audio the encoder writes a temp file that the mux at close turns into <name>.<ext>
	if ( Com_sprintf( vp.videoPath, sizeof( vp.videoPath ), audio ? "%s.video.%s" : "%s.%s", name, codec->ext ) >= (int)sizeof( vp.videoPath )
		|| strlen( name ) >= sizeof( vp.name ) ) {
		Com_Printf( S_COLOR_YELLOW "video: name %s is too long; recording AVI\n", name );
		return qfalse;
	}

	vp.width = width;
	vp.height = height;
	vp.fps = fps;
	vp.audio = audio;
	vp.rate = rate;
	vp.channels = channels;
	vp.frames = 0;
	vp.audioBytes = 0;
	vp.pcm = 0;
	Q_strncpyz( vp.encoder, enc, sizeof( vp.encoder ) );
	Q_strncpyz( vp.ext, codec->ext, sizeof( vp.ext ) );
	vp.audioArgs = codec->audioArgs;
	Q_strncpyz( vp.name, name, sizeof( vp.name ) );

	// FS_FOpenFileWrite creates videos/ under the homepath; the log is also ffmpeg's stderr
	f = FS_FOpenFileWrite( va( "%s.ffmpeg.log", name ) );
	if ( !f ) {
		Com_Printf( S_COLOR_YELLOW "video: cannot write %s.ffmpeg.log; recording AVI\n", name );
		return qfalse;
	}
	const char *header = va( "video %s: %dx%d %d fps, %s\n", name, width, height, fps, enc );
	FS_Write( header, (int)strlen( header ), f );
	FS_FCloseFile( f );
	Q_strncpyz( logPath, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), va( "%s.ffmpeg.log", name ) ), sizeof( logPath ) );
	Q_strncpyz( outPath, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), vp.videoPath ), sizeof( outPath ) );

	std::string out = cl_videoArgs->string[0] ? std::string( cl_videoArgs->string ) : CL_VideoEncoderArgs( enc, cl_videoQuality->integer );
	std::string filter;
	if ( !strcmp( enc, "ffv1" ) ) {
		filter = va( "-vf vflip -pix_fmt %s", CL_VideoPixelFormat( enc ) );
	} else {
		// the game renders full-range sRGB-primaried RGB: convert with the BT.709 matrix to limited range and say so
		filter = va( "-vf vflip,scale=out_color_matrix=bt709:out_range=tv:flags=accurate_rnd+full_chroma_int,format=%s,"
			"setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=tv"
			" -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv", CL_VideoPixelFormat( enc ) );
	}

	std::string cmd = VP_Quote( vp.ffmpeg ) + va( " -hide_banner -nostats -loglevel info -y -f rawvideo -pix_fmt bgr24 -s %dx%d -framerate %d -i -",
		width, height, fps );
	cmd += " " + filter + " " + out;
	if ( !strcmp( codec->ext, "mp4" ) || !strcmp( codec->ext, "mov" ) )
		cmd += " -movflags +faststart";
	cmd += " " + VP_Quote( outPath );

#ifdef _WIN32
	if ( !VP_Spawn( cmd.c_str(), &vp.pipe, logPath, NULL, &vp.process ) ) {
		Com_Printf( S_COLOR_YELLOW "video: could not start %s; recording AVI\n", vp.ffmpeg );
		return qfalse;
	}
#else
	cmd += " >>" + VP_Quote( logPath ) + " 2>&1";
	vp.pipe = popen( cmd.c_str(), "w" );
	if ( !vp.pipe ) {
		Com_Printf( S_COLOR_YELLOW "video: could not start %s; recording AVI\n", vp.ffmpeg );
		return qfalse;
	}
	setvbuf( vp.pipe, NULL, _IONBF, 0 );	// every write happens on the writer thread (see CL_VideoWriter)
#endif

	if ( audio )
		vp.pcm = FS_FOpenFileWrite( va( "%s.pcm", name ) );

	const size_t frameBytes = (size_t)width * height * 3;
	for ( int i = 0; i < VIDEO_QUEUE_FRAMES; i++ )
		vp.buffers[i].resize( frameBytes );
	vp.head = vp.count = 0;
	vp.stop = vp.failed = false;
	vp.worker = new std::thread( CL_VideoWriter );
	vp.active = qtrue;

	Com_Printf( "video: recording %s.%s, %dx%d at %d fps with %s%s\n", name, codec->ext, width, height, fps, enc,
		audio ? " (audio muxed at stop)" : "" );
	return qtrue;
}

qboolean CL_VideoPipe_Active( void )
{
	return vp.active;
}

qboolean CL_VideoPipe_Failed( void )
{
	if ( !vp.active )	// main thread only, like Open and Close: no lock every client frame
		return qfalse;
	std::lock_guard<std::mutex> l( vp.lock );
	return ( vp.active && vp.failed ) ? qtrue : qfalse;
}

/*
===============
CL_VideoPipe_Frame

data: the renderer's uncompressed AVI frame (BGR, bottom-up rows of stride bytes). Blocks only when the encoder is
VIDEO_QUEUE_FRAMES behind.
===============
*/
void CL_VideoPipe_Frame( const byte *data, int size )
{
	const int line = vp.width * 3;
	const int stride = PAD( line, AVI_LINE_PADDING );
	int index;

	if ( !vp.active || size < stride * vp.height )
		return;

	{
		std::unique_lock<std::mutex> l( vp.lock );
		vp.cond.wait( l, [] { return vp.count < VIDEO_QUEUE_FRAMES || vp.failed; } );
		if ( vp.failed )
			return;
		index = ( vp.head + vp.count ) % VIDEO_QUEUE_FRAMES;
	}

	byte *dst = vp.buffers[index].data();
	if ( stride == line ) {
		memcpy( dst, data, (size_t)line * vp.height );
	} else {
		for ( int y = 0; y < vp.height; y++ )
			memcpy( dst + (size_t)y * line, data + (size_t)y * stride, line );
	}

	{
		std::lock_guard<std::mutex> l( vp.lock );
		vp.count++;
	}
	vp.cond.notify_all();
	vp.frames++;
}

void CL_VideoPipe_Audio( const byte *pcm, int size )
{
	if ( !vp.active || !vp.pcm || size <= 0 )
		return;
	FS_Write( pcm, size, vp.pcm );
	vp.audioBytes += size;
}

/*
===============
CL_VideoPipe_Close

Drains the queue, lets ffmpeg finish the file and, with audio, muxes the PCM in. Blocks until the file is complete.
===============
*/
void CL_VideoPipe_Close( void )
{
	char logPath[MAX_OSPATH];
	int code;
	qboolean ok;

	if ( !vp.active )
		return;
	vp.active = qfalse;

	{
		std::lock_guard<std::mutex> l( vp.lock );
		vp.stop = true;
	}
	vp.cond.notify_all();
	vp.worker->join();
	delete vp.worker;
	vp.worker = NULL;

#ifdef _WIN32
	CloseHandle( vp.pipe );
	code = VP_Wait( vp.process );
#else
	code = pclose( vp.pipe );
#endif
	vp.pipe = NULL;
	ok = ( code == 0 && !vp.failed ) ? qtrue : qfalse;

	for ( int i = 0; i < VIDEO_QUEUE_FRAMES; i++ )
		std::vector<byte>().swap( vp.buffers[i] );

	if ( vp.pcm ) {
		FS_FCloseFile( vp.pcm );
		vp.pcm = 0;
	}

	Q_strncpyz( logPath, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), va( "%s.ffmpeg.log", vp.name ) ), sizeof( logPath ) );

	if ( ok && vp.audio ) {
		char videoOS[MAX_OSPATH], pcmOS[MAX_OSPATH], outOS[MAX_OSPATH];
		Q_strncpyz( videoOS, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), vp.videoPath ), sizeof( videoOS ) );
		Q_strncpyz( pcmOS, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), va( "%s.pcm", vp.name ) ), sizeof( pcmOS ) );
		Q_strncpyz( outOS, FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), FS_GetCurrentGameDir(), va( "%s.%s", vp.name, vp.ext ) ), sizeof( outOS ) );

		if ( vp.audioBytes > 0 ) {
			std::string cmd = VP_Quote( vp.ffmpeg ) + " -hide_banner -nostats -loglevel warning -y -i " + VP_Quote( videoOS );
			cmd += va( " -f s16le -ar %d -ac %d -i ", vp.rate, vp.channels ) + VP_Quote( pcmOS );
			cmd += va( " -map 0:v -map 1:a -c:v copy %s%s ", vp.audioArgs,
				( !strcmp( vp.ext, "mp4" ) || !strcmp( vp.ext, "mov" ) ) ? " -movflags +faststart" : "" ) + VP_Quote( outOS );
			code = VP_Run( cmd.c_str(), logPath, NULL );
			ok = ( code == 0 ) ? qtrue : qfalse;
		} else {
			// nothing was heard: the video is the file
			ok = ( rename( videoOS, outOS ) == 0 || ( remove( outOS ) == 0 && rename( videoOS, outOS ) == 0 ) ) ? qtrue : qfalse;
		}
		if ( ok ) {
			FS_HomeRemove( vp.videoPath );
			FS_HomeRemove( va( "%s.pcm", vp.name ) );
		}
	}

	if ( ok ) {
		FS_HomeRemove( va( "%s.ffmpeg.log", vp.name ) );
		Com_Printf( "Wrote %d frames to %s.%s (%s)\n", vp.frames, vp.name, vp.ext, vp.encoder );
	} else {
		Com_Printf( S_COLOR_RED "video: ffmpeg failed (exit %d) after %d frames; see %s.ffmpeg.log\n", code, vp.frames, vp.name );
	}
}
