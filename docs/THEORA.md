# Theora video playback in xenjk

xenjk supports modern Ogg Theora (`.ogv` / `.ogm`) cinematics anywhere
the legacy RoQ codec used to work — the `cinematic` console command,
`videoMap` shader stages, ICARUS `SET_VIDEO_PLAY` opcodes, and any
custom UI menu element that calls into the engine's cinematic API.

The original RoQ pipeline still runs by default; Theora is gated behind
a build flag so the default xenjk build is bit-identical to vanilla
OpenJK if you don't want it.

## Why bother

The original RoQ decoder was authored in 1999 and caps cinematic
textures at 512×512. On a 4K monitor that gets stretched to ~1500 pixels
and looks visibly soft. Theora plays at native source resolution
(1080p, 1440p, 4K — pick what fits your file size budget) and at the
same on-disk size delivers ~3× the visual quality of RoQ on motion-heavy
content.

For a 30-second 1080p loop:
- RoQ (re-encoded to taste): ~50–100 MB
- Theora at q=7: ~15–30 MB

## Building with Theora support

```cmd
build_and_install.bat Release Win32 nop theora
```

Or via raw CMake:

```cmd
cmake -G "Visual Studio 17 2022" -A Win32 -DBuildTheora=ON ..
cmake --build . --config Release --parallel
```

When `BuildTheora=ON`, the four bundled Xiph libraries
(`lib/{ogg,vorbis,theora,theoraplay}/`) build as static libs and link
into `openjk.x86.exe`. The new file `codemp/client/cl_cin_theora.cpp`
adds the per-handle backend.

When `BuildTheora=OFF` (the default), none of the above gets compiled.

## Encoding a `.ogv` for xenjk

The reference encoder is `ffmpeg` with `libtheora` + `libvorbis`. Install
ffmpeg from <https://www.gyan.dev/ffmpeg/builds/> on Windows, or
`apt install ffmpeg` / `brew install ffmpeg` elsewhere.

### Recipe 1 — menu loop (looping silent video, square aspect)

The vanilla menu video is 256×256 and silent. Match those constraints
so the output behaves identically; the renderer will scale it to
whatever rect the menu shader specifies.

```sh
ffmpeg -i input.mp4 \
       -vf "scale=2048:2048:force_original_aspect_ratio=increase,crop=2048:2048" \
       -t 30 \
       -c:v libtheora -q:v 7 \
       -an \
       output.ogv
```

- `scale...crop` produces a 2048×2048 square; downscaled to 1536×1536
  on a 4K screen, that reads sharp.
- `-t 30` caps duration at 30s (typical menu loop length).
- `-q:v 7` is "near-lossless"; values 1–10. Drop to 5 for smaller files
  with mild quality loss.
- `-an` drops audio. The menu plays muted anyway.

### Recipe 2 — story cinematic (full audio + video)

```sh
ffmpeg -i input.mov \
       -vf "scale=1920:1080:force_original_aspect_ratio=decrease,pad=1920:1080:(ow-iw)/2:(oh-ih)/2" \
       -c:v libtheora -q:v 6 \
       -c:a libvorbis -q:a 5 -ac 2 -ar 44100 \
       output.ogv
```

- Stereo Vorbis at quality 5 is roughly 96–112 kbps.
- 1920×1080 is fine; the engine no longer enforces power-of-two sizes.

### Recipe 3 — looping in-map TV / videoMap

```sh
ffmpeg -i input.mp4 \
       -vf "scale=512:384" \
       -t 10 \
       -c:v libtheora -q:v 6 \
       -an \
       output.ogv
```

A 512×384 loop is plenty for an in-game TV brush; it'll stretch to
whatever the brush face is.

## Dropping a `.ogv` into a slot the engine expects to be `.roq`

Easiest approach for replacing a vanilla video without editing any
shader files: package your `.ogv` into a small pk3 named
alphabetically *after* `assets3.pk3` (e.g. `z_mymod.pk3`), and inside
that pk3 store the file with a `.roq` extension at the path the engine
already expects.

```
z_mymod.pk3 contents:
    video/ja01.roq        <-- actually Ogg Theora content
```

The engine's dispatch checks the file's first four bytes; an `OggS`
magic header routes the file to the Theora backend regardless of its
`.roq` extension, while real RoQ files (which start with `0x84 0x10`)
go through the legacy decoder.

Alternative: ship your file with a real `.ogv` extension and override
the shader to point at it. Shader file in any `shaders/*.shader` inside
your pk3 will override stock shaders by name.

## Verifying it's playing through the Theora path

In the ingame console (`~`):

```
\cinematic ja01           # extension auto-resolved to .roq
\cinematic test.ogv       # explicit path
```

If routing succeeded you'll see no error in the console. If it failed,
common error lines:

| Error | Meaning |
|---|---|
| `CIN_THEORA_Open: cannot open <path>` | FS layer can't find the file — check pk3 packaging |
| `CIN_THEORA_Open: TheoraPlay failed to start` | File isn't valid Theora — re-encode |
| `gfx/menus/videologo has a stage with no image` | Engine fell through to RoQ and that failed too — usually means the dispatch order missed the file |

## Cvars

There are no cvars specific to Theora. The general cinematic system
uses `cl_inGameVideo` (vanilla) which we haven't touched.

## Limitations

- The whole `.ogv` is loaded into memory at play time so the worker
  thread can read without touching JKA's non-thread-safe FS layer.
  For a 30-sec menu loop this is ~30 MB; for a feature-length cinematic
  it'd be hundreds. Streaming I/O is on the future-work list.
- Audio-only Ogg files aren't validated — they'll start the decoder but
  produce no frames. The engine just shows the previous content.
- Mid-stream decode errors are terminal; the cinematic stops on the
  last good frame. Re-encoding the source file fixes most cases.

## Cherry-picking just the Theora support into your own fork

```sh
git remote add xenjk https://github.com/RebirthMeow/xenjk.git
git fetch xenjk
git cherry-pick <theora-commit-hash>      # see git log for the exact hash
git cherry-pick <pot-lift-commit-hash>    # required dependency
```

The Theora and POT-lift commits are the only required pieces; sound
indicators and Bot2 are independent.
