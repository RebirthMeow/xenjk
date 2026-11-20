# Renderer bench (setup 2026-10-08)

Owner direction: Vulkan performance with rend2's look. This bench compares the candidates on the same demo before
any port is chosen. Survey and background: the 2026-10-08 renderer survey in the session record; candidates are
listed with notes in `tools/fixtures/renderer_bench/candidates.json`.

| Candidate | Tree (read-only clone) | Renderer |
| --- | --- | --- |
| xenjk-vanilla | this repo | rd-vanilla |
| xenjk-rend2 | this repo | rd-rend2 (OpenJK master, dormant upstream) |
| somaz-rend2 | `../SomaZ-OpenJK-rend2` (`rend2-unified-wip`) | rd-rend2, where rend2 is developed |
| tayst-rend2 | `../TaystJK` | rd-rend2t |
| tayst-vulkan | `../TaystJK` | rd-vulkant (JKSunny master, Quake3e lineage) |
| jksunny-pbr | `../JKSunny-EternalJK` (`pbr`) | rd-vulkan with rend2-style materials |

**2026-10-12:** JKSunny's rd-vulkan (perf/vulkan d4090e931) is ported into xenjk and installed as the xenjk client
(`GameData/xenjk.x86.exe` + `rd-vulkan_x86.dll`, Vulkan by default with an rd-vanilla fall back): see
renderer-vulkan-in-xenjk.md. Candidate `xenjk-vk-installed-sun` runs it in place.

## Running

```
python tools/renderer_bench.py build                      # every tree once, out of source, ~10-20 min each
python tools/renderer_bench.py run --demo kejim-runner    # all candidates x default,max x 3 reps, plus a shots pass
python tools/renderer_bench.py report .codex-research/renderer-bench/runs/<run>
```

Outputs go to `.codex-research/renderer-bench/`: `builds/<tree>-<rev>/` (with `bench_build.json` and a build log),
`runs/<stamp>-<demo>/<candidate>__<scenario>__<rep>/` (private bin, homepath, `qconsole.log`, `run.json`),
`report.md`/`report.json`, and `shots_sheet.jpg` (a row per candidate and scenario, a column per shot frame).

## Method

- Each candidate runs its own engine and renderer; game, cgame and ui come from the installed GameData for all of
  them, so only engine and renderer differ. The installed game is read as `fs_basepath` and never written.
- Private and offline: own bin and homepath per run, `net_enabled 0`, `cl_motd 0`, muted, windowed 1920x1080, vsync
  off, no fps cap. Builds turn off Discord, the dedicated server, game modules, SP, tests and the libjpeg-turbo
  download.
- Frame data: **PresentMon** (Intel's ETW present tracker): Intel's standalone CLI
  PresentMon-2.6.0-x64.exe (GitHub release v2.6.0, Intel-signed; downloaded 2026-10-08 with the owner's OK), named by
  the environment variable `PRESENTMON` (else found at the Intel and FrameView install paths); FrameView's bundled copy exits 1 even elevated. `--presentmon
  <path>` or `off`; `presentmon-check` tests a 5 s capture from the current shell. One capture per timing run, filtered to the client's exe, ending when the client exits. Reported:
  frame time mean/p99/p99.9/max, frames over 16.7/33.3/50 ms and mean GPU time, from the last N presents where N is the
  timedemo's frame count (the capture also holds startup and loading). **It needs an elevated shell**: run the bench
  from an administrator terminal, or add the account to the "Performance Log Users" group yourself (`lusrmgr.msc`,
  then sign out and in) for unattended overnight runs. Without it the run notes "presentmon exited ... (not
  elevated?)" and keeps the com_speeds numbers.
- Timing: `timedemo 1` plays every demo snapshot as fast as the client can draw; `com_speeds 1` logs one line per
  frame (`all`, renderer front end `rf`, back end `bk`, in whole ms) into a buffered `logfile 1`. Reported: timedemo
  fps, mean/p99/max frame ms, frames over 16/33/50 ms (the hitches), mean rf/bk, and startup-plus-load seconds (wall
  time minus timedemo time: shader compilation and map load land here).
- Looks: a separate timedemo pass takes screenshots after fixed frame counts (`scenarios.json` "shots"), so they
  don't disturb timing; timedemo advances one snapshot per frame, so the moments match across candidates.
- Scenarios: `default` (each renderer's defaults) and `max` (HDR, SSAO, sun shadows, normal/specular mapping with
  generated normal maps, cubemaps, glow, 4x MSAA, bloom where they exist). Base JKA has no PBR materials; a PBR
  content-pack scenario is a later addition.
- Runs are one at a time (timing runs alone). Three reps per pair; the first rep of a fresh build also carries the
  driver's shader-cache warm-up.

## Caveats

- `com_speeds` prints a console line per frame; the cost is the same for every candidate but lowers absolute fps.
- Whole-millisecond frame times: at a few hundred fps the mean is coarse; hitches (>16 ms) are what it resolves.
- A candidate can fail on the demo (cgame API or protocol differences); the run then shows `timeout` or no frames.
- The xenjk tree may be mid-edit: its build directory name carries `-dirty` then. Prefer a clean commit.
- First-run renderer menus or settings dialogs, if a client opens one, would stall a run until its timeout (900 s).

## Scene bench and first results (2026-10-08)

The owner's direction moved the bench off demos and PresentMon (it needs elevation, so it could not be run
unattended): `tools/renderer_scenes.py` visits the owner's saved viewpoints on maps with real rend2 content
(`tools/fixtures/renderer_bench/scenes.json`: ctf_illimiran_pbr, ctf_dantooine_pbr_clean, siege_urban_pbr,
siege_urban_cyber; packs copied from `<newmod tree>/rend2/GameData/base` without newmod into
`.codex-research/renderer-bench/content`, used as `fs_cdpath`), 3840x2160 borderless at a 240 fps cap, each renderer
on the owner's rend2 preset (capture.py REND2_ON) in its own cvar names. Frame times come from `cgame/cg_bench.c`
(`benchview <name> <frames>`, frame-to-frame time on QueryPerformanceCounter, GPU waits included). The same
logger is added to JKSunny's cgame in a worktree (`.codex-research/renderer-bench/src/jksunny-bench`), because
EternalJK refuses xenjk's cgame; EternalJK also unpacks game DLLs from pk3s first, so the cgame is shipped in a
last-sorting `zzzz_bench_cgame.pk3`.

Lessons: the engine keeps at most 32 `+` commands (settings go in autoexec.cfg); exclusive fullscreen unattended
loses focus and renders black at ~48 fps (borderless window instead); `com_speeds` work time excludes GPU waits
(misleading at 4K); a killed client leaves the desktop gamma ramp changed (`r_ignorehwgamma 1`, polite WM_CLOSE);
back-to-back starts after a crash crashed (5 s gap); TaystJK writes `home/taystjk`, EternalJK `home/EternalJK`.

First results (`.codex-research/renderer-bench/scenes/20261008T185305/`, one rep, report.md and sheet_*.jpg): all four
near the cap on Dantooine/Illimiran; Cyber heaviest for all. Avg fps / frames over 16.7 ms: SomaZ rend2 250/1, 249/4,
224/1, 239/20; TaystJK rend2 247/3, 249/1, 203/12, 237/24 (crashed once at quit); TaystJK Vulkan 250/0, 250/0,
248/3, 222/8 (stock look); JKSunny PBR 248/3, 247/7, 248/2, 240/13. Looks: SomaZ and TaystJK rend2 nearly identical;
TaystJK Vulkan stock with a blown-out sky; JKSunny PBR has PBR materials but a broken (grid) sky on every map and
a darker image. Owner (2026-10-08): JKSunny PBR vs either rend2 is a large visual gap; e.g. siege_urban_pbr's first
view shows shadows cast by the lights in JKSunny that rend2 does not show.

## Capability vs asset (code and pack reading, 2026-10-08; not yet checked on screen)

- Grid sky: an asset/parser issue, not a missing feature. Every xen sky material starts with `q3gl2_sun`; JKSunny's
  general-keyword parser has no case for it and returns failure (`rd-vulkan/tr_shader.cpp:2967`). The whole sky
  material then becomes the default shader (`:3516`), which draws the grid image. The fix is one parse case, or
  `q3map_sunExt`, which it accepts (`:2788`).
- Urban light shadows: JKSunny has no shadow maps and does not create lights from map `light` entities. The shadows
  it shows are most likely the lightmap's baked ones. rend2 at `r_sunlightMode 2` adds a sun term over the lightmap
  and fills them in. To check, run rend2 at `r_sunlightMode 0`/`1`.
- Real capability gaps in JKSunny: sun light and cascaded sun shadows, parallax (it parses `parallaxDepth` but never
  renders it), tone mapping/exposure (`r_hdr` only picks the framebuffer format), point-light shadow maps, cubemap
  bounces, `refractive`/`cloth`. Same in both: `rmoMap`/`normalMap`/`normalHeightMap`/spec-gloss parsing, suffix
  discovery, `misc_cubemap` probes baked at load, SSAO, deluxe specular.
- Content: LDR internal lightmaps and light grid, no deluxemaps, HDR lightmaps or cubemap images; `misc_cubemap`
  probes on every map. siege_urban_pbr reuses v10's lighting; cyber was rebaked much darker.
- Preset hygiene: `r_pbrTextures` and `r_cubemapSize` exist in neither renderer; `r_dlightMode` means point-light
  shadows in rend2 but per-pixel dlights in JKSunny.

## Best vs best, siege_urban_pbr (2026-10-08)

`renderer_scenes.py run --maps siege_urban_pbr --candidates somaz-rend2,jksunny-pbr --reps 3 --profile best`
(`.codex-research/renderer-bench/scenes/20261008T193831-best/`). The profile (`scenes.json` "profiles") adds 4x MSAA
and 16x anisotropy to both. JKSunny also gets its 16-bit framebuffer, bloom and deluxe specular, plus copies of the
xen materials with `q3gl2_sun` replaced by `q3map_sunExt`, shipped in its run's homepath.
- Sky fixed: JKSunny draws the urban sky on every view.
- Speed: JKSunny 224-239 fps (1-8 frames over 16.7 ms). rend2 173-179 fps (1-2 over 16.7 ms); 4x MSAA costs rend2
  about 45 fps.
- Look:
  - rend2 has sun shadows under cars and props, a deeper sky and more contrast.
  - JKSunny has no cast shadows. Its models are lit flat, and bloom at the default threshold (0.05) washes the image
    and halos the lamp-lit walls.
- Caveat: JKSunny's own cgame widens the FOV at 16:9 (`cg_fovAspectAdjust 1`), so its views show more of the scene.
  Set it to 0 for matched framing.

Headroom and sun cost (profiles `best,uncapped` and `best,nosun,uncapped`, 3 reps each, RTX 3080). Medians of the
per-view median frame time:

| | ms/frame |
| --- | --- |
| JKSunny PBR | 3.90 (about 256 fps) |
| rend2 | 5.02 |
| rend2 with no sun term and no sun shadows | 4.60 |

The sun costs rend2 0.42 ms (0.36-0.47 per view, about 8%). Added at the same cost, JKSunny would be about 4.3 ms
(about 230 fps), still faster than rend2 without the sun. Hitches over 16.7 ms are similar for both, 5-13 per rep
uncapped.

## rend2 performance and latency review (SomaZ `shared/rd-rend2`, code reading 2026-10-08, not yet measured)

- Click latency:
  - rend2 keeps 2 frames in flight (`tr_local.h:66` `MAX_FRAMES 2`).
  - The fence wait (`tr_cmds.cpp:534`) runs in `RE_BeginFrame`, after `CL_SendCmd` has already read input.
  - `r_finish` is registered but never read.
  - When GPU-bound, a click waits behind about 2 queued frames plus the driver's queue. Vanilla drains its queue
    often (`qglFinish` with dynamic glow, flare `glReadPixels`).
  - Borderless OpenGL may also be composed by DWM.
  - The engine loop itself (sleep, then input, then render) is identical for every renderer.
- Performance candidates, from the review's ranked list:
  - Glow chain: runs at full 4K, always on, with unneeded clears.
  - Parallax: up to 26 fetches per pixel, with no distance or angle falloff.
  - Early-Z: `USE_ALPHA_TEST` is compiled into every program, which defeats it.
  - Sun shadows: the PCF lookup runs on surfaces facing away from the sun.
  - Post-process: the depth blit and refraction pass always run.
  - SSAO: a full-res R32F depth copy.
  - The review's claim that `GL_TextureMode` runs every frame is wrong: it is guarded by `modified`
    (`tr_cmds.cpp:659`).
- Bugs:
  - Cascade texel snapping uses `static` sizes from cascade 0 for all three cascades (`tr_main.cpp:3044`).
  - With MSAA, SSAO reads the previous frame's resolved depth (the main view's `targetFbo` check is NULL,
    `tr_backend.cpp:2190`).
  - The UBO flush offset (`tr_vbo.cpp:881`).
  - A repeated tap in `dglow_downsample.glsl:31`.

## rend2 GPU pass breakdown (2026-10-08)

Candidate `somaz-rend2-prof` is SomaZ 788c19e9 plus per-pass GPU timed blocks: worktree
`.codex-research/renderer-bench/src/somaz-prof`, branch `bench/gpu-timers`, commit f59b2ef3b. Profile `gputimers` sets
`r_speeds 100`; `tools/renderer_gpu_passes.py <run>` gives the median ms per block and view. siege_urban_pbr with the
`best` settings (4K, 4x MSAA), mean of the five views:

| block | ms |
| --- | --- |
| sun cascades (3 views) | 0.08 total |
| main: depth prepass | 0.15 |
| main: SSAO | 0.30 |
| main: lit pass | 3.61 |
| post: MSAA resolve | 0.33 |
| post: glow chain | 0.46 |
| post: copy + depth blit | 0.19 |
| post: glow composite | 0.05 |
| post: refraction | 0.00 |

- `r_parallaxMapping 0`: the lit pass drops from 3.61 to 1.02 ms. Frame-to-frame on the normal build, uncapped: 331-342 fps
  vs 196 with parallax (JKSunny without parallax: 256).
- `r_sunlightMode 0` + `r_sunShadows 0`: the lit pass is 2.78 ms (the sun costs about 0.83 ms GPU there, 0.42 ms of
  frame time).
- Parallax is about 2.6 ms of a 5 ms frame. The urban materials use small depths (0.01-0.02); the visible
  difference is extra relief on paving and stone. The shader runs 16 linear + 8 binary + 2 fetches per pixel with no
  distance or angle falloff (lightall.glsl:627-695). A cheaper parallax is the main rend2 performance item.
- Note: scene views are not pixel-identical between runs (pitch is set with timed `+lookdown`), so screenshot pairs
  are approximate.

## Latency test (unattended, no admin rights)

`python tools/renderer_latency.py run --candidates a,b [--profile best,...] [--reps 3] [--samples 100]`

- **Probe:** `tools/latency/latprobe.cpp`, built by `tools/latency/build_latprobe.cmd` into `.codex-research/latency/bin/`.
- **Each run:** the client starts like a scene run, holds still as a spectator at a saved view, and
  `latprobe` brings it to the front.
- **Each sample:** one relative mouse move injected with `SendInput`, then the time to the first DXGI Desktop
  Duplication frame whose centre region changed (its `LastPresentTime`). Moves alternate, so the view returns.
- **What it covers:** input queueing, the game frame, its GPU queue, presentation and composition. Not mouse polling
  or the monitor, so it's a relative measure for before/after work.
- **Why not PresentMon:** it needs admin rights or the "Performance Log Users" group.
- **While it runs:** it uses the main monitor and the real mouse.
- **Output:** results in `.codex-research/latency/runs/<stamp>/report.md`.

Baseline (siege_urban_pbr start view, `best` settings, 3 reps x 100 samples; median, p10-p90 in ms):

| | 240 cap | uncapped |
| --- | --- | --- |
| SomaZ rend2 | 15.3 (15.0-15.8) | 15.3 (15.0-15.8) |
| JKSunny PBR | 8.0 (1.0-8.5) | 35.8 (34.4-37.3) |

- **Agreement with PresentMon:** the capped rend2-JKSunny gap (7.3 ms) matches the owner's earlier PresentMon test
  (9.2 vs 2.1 ms, gap 7.1).
- **Open questions:** JKSunny uncapped (36 ms) and its capped samples near 1 ms are unexplained. Desktop Duplication
  may change the presentation mode; treat absolute numbers with care.

## rend2 perf branch and urban_v2 materials (2026-10-08)

**Code:** `perf/rend2` in the SomaZ worktree (f26634fcb), candidate `somaz-rend2-perf`.
- `r_maxFrameLatency` (default 1).
- `r_parallaxFast` (default 1): parallax only where it can shift the image by at least a pixel, with steps scaled
  to that shift.
- `r_dynamicGlowSkipEmpty` (default 1): skip the empty glow chain, depth copy and refraction pass.
- Sun shadow lookups skipped on unlit sides.
- No dead clears in the glow chain.
- Per-cascade texel snapping (fixes cascades 2-3 shimmering).

**Content:** profile `urban_v2` loads `.codex-research/renderer-bench/variants/urban_v2/zzzz_urban_v2.pk3`, built
by `tools/pbr_heights.py build-urban`. Parallax stays on 21 materials with real relief (masonry traced as a mortar
grid, tile/panel grooves, planks, containers, grating, rubble), with full-range heights. The other 31 use plain
normal maps.

siege_urban_pbr, `best`, uncapped, 2 reps:

| | original materials | urban_v2 |
| --- | --- | --- |
| stock rend2 | 200 fps | 315 fps |
| perf rend2 | 265 fps | 363 fps |
| JKSunny PBR | 256 fps | 270 fps |

**Lit pass:**

| | ms |
| --- | --- |
| stock rend2 | 3.61 |
| perf rend2 | 2.13 |
| perf rend2 + urban_v2 | 1.12 (parallax off entirely: 1.02) |

- Post-processing is now the largest remaining cost: 0.82 ms. When the lamps glow, the full-res glow chain is
  0.45 ms; the MSAA resolve is 0.33 ms.

**Latency (240 cap, median of 300 samples):**

| | ms |
| --- | --- |
| stock rend2 | 15.3 |
| perf, `r_maxFrameLatency 1` | 4.9 |
| perf, `r_maxFrameLatency 0` | 5.8 |
| perf, `r_maxFrameLatency 2` (old queue) | 4.9 |

- At the cap the gain comes from the GPU speedup: frames now fit under the cap, so no queue forms.
- Uncapped: perf with `r_maxFrameLatency 1` is 10.5 ms, stock 15.3.
- The GPU-bound queue test (perf with `oldparallax`, `r_maxFrameLatency` 1 vs 2) is still to run.

**Look (screenshot pairs):**
- Masonry now shows real recessed mortar.
- Plaster and the stone door trims lost the bumpy relief the old noise parallax gave them; they read flatter.

## JKSunny latency fix, settings and pass breakdown (2026-10-08)

Candidate `jksunny-pbr-perf` is the worktree `.codex-research/renderer-bench/src/jksunny-perf`, branch
`perf/vulkan`, commit 06e30f0c1. It adds `r_maxFrameLatency` (a fence wait after present) and `r_gpuTimes 1`
(timestamps around every render pass).

**GPU per render pass** (`best`, uncapped; mean of the five views, ms):

| pass | ms |
| --- | --- |
| main | 1.04 |
| SSAO extract | 0.47 |
| SSAO blur | 0.48 |
| SSAO blend (MSAA reload) | 0.29 |
| dglow extract (MSAA) | 0.20 |
| dglow blend (MSAA reload) | 0.24 |
| bloom extract | 0.09 |
| bloom blend (MSAA reload) | 0.41 |
| gamma | 0.11 |
| frame (first to last pass) | 3.47 |

Post-processing takes about 2.3 of 3.5 ms. SSAO is 1.24 ms, four times rend2's 0.30. The three MSAA blend reloads
add up to 0.94 ms.

**fps, uncapped:**

| settings | fps |
| --- | --- |
| `best` | 278 |
| `r_bloom 0` | 330 |
| `r_hdr 0` | 377 |
| both | 439 |

**Latency:**
- At the 240 cap, the stock build's runs differ widely (medians 7.9 and 4.9 ms). One capped run of the fixed build
  gave 4.4 ms; the other crashed at start.
- Uncapped, stock is 37.3 ms (35.4-38.4). The fixed build's median is 2.7 ms, but its p90 is 36.5: the fence fix
  removes most of the queue, and about a fifth of samples still wait in a second queue, probably the swapchain or
  presentation.

**rend2 queue under GPU load** (`oldparallax`, 240 cap):
- `r_maxFrameLatency` 1 and 2 both give 14.4 ms: the GL driver queues inside SwapBuffers.
- `r_maxFrameLatency 0` gives 6.9 ms. Its fps cost is not yet measured.

**Startup crashes:** several EternalJK launches in a row, stock and fixed builds alike, exited 0xC0000005 before
writing anything. Memory was 12 GB free and no event-log entry was written; a later batch ran clean. Treat it as an
environment fault and rerun.

## JKSunny parity steps 1-4, present wait and sun 5a/5b (2026-10-09)

Commits on `perf/vulkan` (worktree `.codex-research/renderer-bench/src/jksunny-perf`), each a pinned candidate in
`candidates.json` (`jksunny-vk-base` = 06e30f0c1, then `-s1` ... `-s6`):

| step | commit | what |
| --- | --- | --- |
| 1 | 40d176ac0 | unknown general/stage keywords skip their line; `q3gl2_sun` parsed as rend2 (map light and shadow scale only with `r_sunShadows`); rend2's sun cvars registered, off |
| 2 | 46950287c | `r_swapchainImages` (latched; 2 = two images without vsync) |
| 3 | dc758321f | one MSAA resolve: SSAO/glow composites, bloom and HUD in a 1x "post 1x" pass |
| 4 | 65f82206c | half-res SSAO (`r_ssao_halfRes`) with a separable bilateral blur (`r_ssao_blurRadius`, default 2); 8-bit glow blur chain; `r_DynamicGlowScale` wired (first blur level size, default 0.5 = old sizes); defaults `r_hdr 0`, `r_dither 1` (`r_bloom` was already 0) |
| 5a/5b | cf5aec729 | sun scene values and the sun term without shadows (camera UBO; rend2's mode 1/2 block and quirks) |
| 2b | 3774a3836 | `r_presentWait` (`VK_KHR_present_wait`; 1 = wait for the frame just presented), default 0 |

- **Shaders:** the machine has no Vulkan SDK. `tools/renderer_spirv.py` compiles JKSunny's GLSL with Khronos' glslang
  (prebuilt 16.6.0 release in `<glslang>`, `bin\glslang.exe`; env `GLSLANG_EXE` overrides), invoked like JKSunny's
  `compile_threaded.cpp` (`-S <stage> -V -o`), and rewrites `shader_data.c`; `verify` checks it against the committed bytes.
  `--compiler godot` uses Godot 4.6's older embedded glslang instead (no geometry shaders).
- **Content:** every run here used the bench's content copy taken before the 2026-10-09 map rebuilds (old packs).

**GPU per pass**, siege_urban_pbr, `best` (4K, 4x MSAA, `r_hdr 1`, bloom on), uncapped, mean of five views (ms):

| pass | base | step 3 | step 4 + owner defaults (`decided`) |
| --- | --- | --- | --- |
| main | 1.05 | 1.02 | 0.65 |
| SSAO extract + blur | 0.47 + 0.47 | 0.47 + 0.48 | 0.12 + 0.07 (two half-res passes) |
| SSAO + glow + bloom blends (MSAA reloads) | 0.29 + 0.24 + 0.41 | in "post 1x": 0.31 | "post 1x": 0.11 |
| dglow extract | 0.20 | 0.20 | 0.09 |
| frame (first to last pass) | 3.87 | 3.24 | 1.26 |

**fps**, same scene, uncapped, one rep each:

| | `best` | `best` + `decided` |
| --- | --- | --- |
| base 06e30f0c1 | 258 | 398 |
| step 3 | 304 | |
| step 4 | 415 | 666 |

The step 3 and base screenshots match; step 4's half-res AO shows no visible change at 4K on these views.

**Latency** (probe median, p90 in ms; `decided` settings for steps 4-6):

| | uncapped | 240 cap |
| --- | --- | --- |
| step 2, 3 swapchain images | 2.3, 37.1 | 8.1, 9.1 |
| step 2, `r_swapchainImages 2` | 11.5, 12.0 | |
| step 4, 3 images | 1.7, 36.4 | |
| step 4, `r_swapchainImages 2` | 7.0, 7.6 | |
| step 6, `r_presentWait 0` | 1.4, 36.5 | 1.8, 2.1 |
| step 6, `r_presentWait 1` | 1.8, 3.2-3.5 | 1.8, 2.2 |
| step 6, `r_presentWait 2` | 3.4, 5.0 | |
| rend2 perf (2026-10-08) | 10.5 | 4.9 |

- At the 240 cap the target is met by the speed work alone: frames now take about 1.3 ms of GPU, so no queue
  forms (1.8 ms against rend2's 4.9).
- Uncapped, the 36 ms mode (about one sample in five) is presentation queueing. `r_presentWait 1` removes it with
  the same median, but it holds the client near the display rate: 278 fps against 640 uncapped. A 2-image
  swapchain also removes it but raises the median to 7 ms. Both stay off by default (owner's call).

**Sun 5a/5b:** with `r_sunlightMode 2` and `r_sunShadows 1` the main pass costs 0.01 ms more. Pairs in
`.codex-research/renderer-bench/scenes/20261009T031340-best-decided/` (rows: JKSunny sun off, JKSunny sun on, rend2
`r_sunShadows 0`): sun-facing ground and walls brighten as in rend2. Not yet like rend2:
- Models: rend2 lights the police car white; JKSunny's stays grey (models are stage (e)).
- `colorScale` (mapLightScale) is computed but not applied to stage colours and the sky; only Dantooine's 0.85
  differs from 1.
- With no shadow maps the sun also reaches covered ground, until stages (c) and (d).

**rend2 `r_maxFrameLatency 0`** (open check, settled): under GPU load (`oldparallax`, uncapped) it costs 25% fps,
211 -> 159, far over the owner's 5% bound, so `perf/rend2` keeps 1.

**Startup crashes (solved later the same day):** JKSunny clients exited 0xC0000005 within seconds of launch in
streaks (5 of 8 launches one batch, 4 in a row another), and once mid-run, for every build including the stock one,
with no WER entry. Disabling the implicit Vulkan layers for the client did not stop them. An unhandled-exception
report added to the engine (`crashlog-seh-<pid>.txt` beside the executable) showed the cause: `CON_Show` (win32
console echo of every `Com_Printf`) calls `WriteConsoleOutputA` with a `CONSOLE_SCREEN_BUFFER_INFO` left uninitialized
when stdout is not a console screen buffer (the bench redirected it to NUL while the process still had a console).
perf/vulkan 13ed3f18f checks the call; the tools now give each client its own hidden console (`client_popen` in
`renderer_bench.py`), which also covers older builds. The next batch ran 4 of 4 launches clean. The tools still retry a
start crash up to three times and record each attempt in `run.json`.

## Sun stages 5c/5d and the map light scale (2026-10-09)

- **Map light scale** (e2a371813): `colorScale` (q3gl2_sun mapLightScale, read with `r_sunShadows`) scales the
  lightmap/vertex/entity light in the lit shaders and the skybox. Unlit world stages are not scaled yet. Only
  Dantooine (0.85) differs from 1.
- **Cascades and PCF** (13ed3f18f, candidates `jksunny-vk-s7` / `-s7-sun`): rend2's three cascades, drawn before the
  main pass into a 3-layer D16 array, sampled with rend2's PCF through an 11th descriptor set. Details in
  [renderer-vulkan-sun-port.md](renderer-vulkan-sun-port.md). `r_sunShadowDebug` (cheat) shows the cascades (1) or
  the shadow value (2).
- **Cost**, siege_urban_pbr 4K, owner preset (2048 maps, filter 2), uncapped: sun shadow pass 0.09 ms (target 0.15),
  main pass 0.65 -> 0.76 ms, GPU frame 1.26 -> 1.46 ms, 640 -> 515 fps (the three cascade views also cost CPU).
- **Look** (`.codex-research/renderer-bench/scenes/20261009T041225-best-decided/`, rows: JKSunny shadow value,
  JKSunny, rend2): cars, boxes and buildings cast shadows where rend2's do; JKSunny's sunlit street is a little
  darker, and models are not lit by the sun yet (rend2 lights the police car white). Dantooine pair:
  `scenes/20261009T034915-best-decided/` (taken before the descriptor fix below, so its JKSunny row is unshadowed).
- **Two bugs found on screen:** the shadow descriptor set was lost when the renderer reset its descriptor pool (level
  resources), and other pipeline layouts disturbed the never-changing set; both fixed before the commit.

## Content refresh, baseline, model brightness and parallax port (2026-10-09, R2)

**Content:** the bench copy now holds M's rebuilt packs (`python tools/renderer_scenes.py content`; changed copies are
moved to `content-old/<stamp>/`, never deleted). `content/manifest.json` records sizes, mtimes and sha256 (map packs)
and an id that every run stores in `run.json` and `report.md`: **90f14a4543d5** = the 2026-10-09 rebuilds (urban
f4b544ec, Dantooine 1da2a9ad, Illimiran 3414349a, Cyber bc129cb7). Runs before 04:24 on 2026-10-09 used the old packs
(`content-old/20261009T042431/`). Every number in this section is on 90f14a4543d5.

**Baseline** (`scenes/20261009T042551-best-decided-uncapped`, one rep, uncapped fps):

| map | JKSunny 13ed3f18f, sun off | JKSunny 13ed3f18f, owner sun preset | rend2 perf |
| --- | --- | --- | --- |
| siege_urban_pbr | 625 | 550 | 350 |
| ctf_dantooine_pbr_clean | 544 | 374 | 328 |
| ctf_illimiran_pbr | 618 | 494 | 412 |
| siege_urban_cyber | 345 | 235 | 301 |

GPU ms, mean of each map's views (`gputimers`; rend2 `scenes/20261009T043319-*`, JKSunny with the sun
`20261009T043820-*`; `renderer_gpu_passes.py <run> --candidate <c> --map <m>`, `--map` added today):

| map | rend2 main view / lit pass / post | JKSunny frame / sun shadow / main |
| --- | --- | --- |
| urban | 1.61 / 1.11 / 0.82 | 1.47 / 0.09 / 0.76 |
| Dantooine | 1.63 / 1.17 / 0.93 | 1.62 / 0.10 / 0.87 |
| Illimiran | 1.14 / 0.62 / 0.95 | 1.43 / 0.11 / 0.67 |
| Cyber | 1.74 / 1.20 / 0.94 | 2.66 / 0.14 / 1.73 |

- **M's new parallax in rend2** (`noparallax` run `20261009T043630-*`): Illimiran lit pass 0.62 vs 0.56 ms (+0.06),
  Cyber 1.20 vs 1.15 ms (+0.05). Cheap.
- **JKSunny's sun costs CPU, not GPU:** its GPU frame stays about 1.5 ms but fps drops 12-31% with the sun (Dantooine
  544 -> 374, Cyber 345 -> 235, below rend2 there). The three cascade views build and draw full draw lists on the
  CPU. Open item (cull casters per cascade, a cheaper cascade draw path).
- Cyber's JKSunny main pass (1.73 ms) is heavier than rend2's lit pass (1.20 ms); not yet looked at.

**Model brightness (5e), found and fixed** (perf/vulkan 8b679a8e5): rend2 lights the urban police car white, JKSunny
grey. Probe runs (`lmonly` = `r_lightmap 1`, `nocube`, `nospec`, rend2 `nosun`; sheets
`.codex-research/renderer-bench/pairs-20261009-r2/probe.jpg`, `car2.jpg`) showed the car is a lightmapped BSP
misc_model whose material (`textures/xen_urb/police`, like the consoles, lamps and cargo) is an `rgbGen vertex`
diffuse plus a `$lightmap` stage. rend2 collapses the two into one lit stage (light = lightmap x vertex colour, sun
added after). JKSunny refused the collapse (non-matching light types), drew the diffuse vertex-lit and multiplied
the result, sun and specular included, by the lightmap. Fixed as rend2 does; the lit shader's diffuse is now the
texture alone in the lightmap and vertex paths (the vertex colour was applied twice). Result
(`pairs-20261009-r2/sheet_*.jpg`, `fix.jpg`): consoles, lamps and props match rend2; the police car is much lighter
but still greyer than rend2's (lightmap only: rend2 about 0.76 on its side, JKSunny about 0.55; not the sun, rend2
`nosun` keeps 0.76). Open.

**Ghoul2 bone cache (5e)** (c659f743f): the cascade views reuse the frame's bone transform and upload (same frame,
scene, bone time, root). Compiled and run on the bench maps, which show no Ghoul2 models: not yet exercised with
players or NPCs.

**Colour scale on unlit stages** (8b679a8e5): rend2 scales every stage colour of a world scene by colorScale
(tr_shade.cpp 1745), so unlit stages and entities too. JKSunny now gives unlit stages a pipeline variant whose
gen_frag output is scaled (spec constant 4, `color_scale_x1000`); the sky uses it too. Only Dantooine (0.85) differs:
mean image brightness moved by up to 1.5/255; not yet compared with rend2 on screen.

**Parallax port (step 6), committed off by default** (8b679a8e5): rend2's `RayIntersectDisplaceMap` /
`GetParallaxOffset` with the perf/rend2 fast form in gen_frag, `r_parallaxMapping` (0 default, 1 on, 2 debug view:
parallax surfaces green, red by the offset), `r_parallaxFast` (1 default). Height flipped to depth in the lookup.
Two JKSunny bugs fixed on the way: `normalHeightMap` was decided after reading the file name, so every explicit
height map became a plain normal map, and creating the normal texture reset `normalScale`/`parallaxDepth`. The first
look run (`scenes/20261009T050606-best-decided`, row "JKSunny dev + parallax" in the urban sheet) shows relief on
the masonry; the debug view, the crop pair against rend2 and the GPU cost (target about 0.1 ms) are not done yet.

## Parallax check and 5g parity pairs (2026-10-09, R3)

**Candidate:** `perf/vulkan` **8b679a8e5** is built pinned (`builds/jksunny-build-8b679a8e5`, from `src/jksunny-build`);
candidates `jksunny-vk-s8` (sun off), `-s8-sun` (owner preset), `-s8-sun-par` (preset + `r_parallaxMapping 1`) and
`-s8-sun-pardbg` (2). Content id 90f14a4543d5. Pairs, sheets and `stats.txt` per map:
`.codex-research/renderer-bench/pairs-20261009-r3/` (made by the new `tools/renderer_parity.py`; scenes
`20261009T053437-best-decided` (capped, parallax), `20261009T054008-best-decided-uncapped` (fps),
`20261009T054501-best-decided-uncapped-gputimers` (GPU), `20261009T053836-*-noparallax` (rend2 without parallax)).

**Parallax on siege_urban_pbr:**
- Debug view (`r_parallaxMapping 2`): green on the masonry, plaster, container and ground planks, orange on a few
  ground and curb patches (the larger offsets); none on the sky, props or models, so it follows the materials M gave
  parallax (sheet row 2 of `scenes/20261009T053437-best-decided/sheet_siege_urban_pbr.jpg`).
- Look: obj1 masonry crop (`pairs-20261009-r3/parallax_urban/view_obj1_crop.jpg`) has the same relief as rend2
  (stepped courses, black joints, sharp silhouettes); no swimming or banding at the 4K crop.
- Cost (`gputimers`, mean of five views): JKSunny main pass 0.752 -> 0.994 ms (**+0.24 ms**), frame 1.45 -> 1.70 ms.
  rend2 on the same views: lit pass 0.948 -> 1.099 ms (+0.15 ms). Over the 0.1 ms target; JKSunny has no depth
  prepass, so its fragments run the ray march for every overdrawn pixel, where rend2 marches only the visible ones
  (the prepass runs parallax only for alpha test). Uncapped fps with the GPU timers on: 663 -> 576.
- Proposal (the owner decides): default `r_parallaxMapping 1` with the sun preset. It matches the owner's rend2
  preset (parallax on) and the cost is 0.24 ms on the heaviest map. If the 0.1 ms target matters, a depth prepass for
  opaque world surfaces (also cheaper for the lit pass overdraw) is the lever; fewer march steps would change the look.

**5g, matched to the owner's rend2 preset** (JKSunny: 8b679a8e5 `-s8-sun-par`, `best,decided`; rend2 `somaz-rend2-perf`;
4K borderless, one rep). Uncapped fps from `...054008...`; GPU ms are the sum of what a frame draws in the timers
(rend2: three sun cascades + main view + post processing; JKSunny: first to last pass):

| map | JKSunny fps | rend2 fps | JKSunny GPU ms | rend2 GPU ms |
| --- | --- | --- | --- | --- |
| siege_urban_pbr | 583 | 393 | 1.70 | 2.48 |
| ctf_dantooine_pbr_clean | 379 | 360 | 2.49 | 2.62 |
| ctf_illimiran_pbr | 537 | 447 | 1.49 | 2.16 |
| siege_urban_cyber | 282 | 334 | 2.42 | 2.78 |

JKSunny is faster on three maps and below rend2 on Cyber (3.5 vs 3.0 ms mean frame, 273 frames over 5 ms): the
sun's CPU cost (open item, not worked on here) and the Cyber main pass (1.63 ms against rend2's 1.21 lit pass).

**Camera caveat:** the scene bench sets the position and yaw with `setviewpos` and the pitch with a timed
`+lookdown`; in these runs the JKSunny view differs from rend2's by a few degrees of yaw/pitch (rend2 repeats itself to
0.1 luma between runs, JKSunny to 1.5-4.8, since its pitch follows the frame time). Per-pixel differences in
`stats.txt` are therefore not a measure; mean luma per view is (within about 1-4 of rend2 where the camera agrees).

**Visible differences that remain** (sheets: `pairs-20261009-r3/5g_<map>/sheet.jpg`, `view_<name>_pair.jpg`, `_crop.jpg`):
- **Dantooine, black quads in the courtyard and lowspawn floors** (cobble path; three dark parallelograms). Present
  in every JKSunny build back to 13ed3f18f and with the sun off (s7), so not the sun or shadows; rend2 draws cobble
  there. A surface that is not drawn or drawn black; not yet traced.
- **Dantooine colour scale (0.85) and sky:** tone matches rend2 (mean luma within 2 on courtyard and westruin; 5 and 8
  on the two views whose camera differs). The sky matches in colour; JKSunny shows the sun disc and flare sprites that
  rend2's frame lacks.
- **Flare sprites in the sky** (a row of small coloured dots at the top of Illimiran mid_low, blue dots on
  Dantooine, bokeh-like spots on Cyber holo4): JKSunny only. Probably the sun's flare/ghost sprites (`r_flares`);
  not checked.
- **Cyber sky/holograms:** the sky shows extra glints, a light cone and a second, fainter hologram that rend2's frame lacks (one view, camera differs);
  mean luma 12 lower than rend2 on holo4 (camera also differs), 4-9 lower on intermission and obj3.
- **Police car:** still greyer (open item owned by a later agent). Urban otherwise reads the same as rend2 (masonry,
  plaster, sun on the walls); rend2 draws a soft ground shadow under the hover cart that JKSunny does not show in
  the start view (the cameras differ there, so unconfirmed).

## Urban sun shadows, aligned camera, depth prepass and probes (2026-10-09, R4)

**Camera fixed first.** The pairs before this used different cameras per renderer: JKSunny's own cgame (EternalJK's,
`builds/jksunny-bench-cgame`) defaults `cg_fovAspectAdjust 1` (wider field of view at 16:9, so JKSunny's start view
looked "further back"), and the pitch came from a timed `+lookdown` that followed the frame rate. Now:
- `cg_fovAspectAdjust 0` in `scenes.json` "common".
- A `benchcam <yaw> <pitch>` cgame command pins the drawn angles exactly (eye from `setviewpos` + view height), in both
  cgames: xenjk's (branch `renderer/benchcam-20261009`, 057ff837 + 7867cb23, worktree `../xenjk-wt-r4`, not
  merged; built as `builds/benchcam-cgame-20261009`, which `renderer_scenes.py` stages) and JKSunny's bench cgame
  (`src/jksunny-bench`, uncommitted bench-only tree as before). The view list's pitch is now used as given.
- Aligned pairs repeat to within the PCF noise (mean |diff| about 0.5 between two runs of one build).

**Content:** M2 refreshed the bench content twice during this work (3b1a43daee81 at 09:30, 4a1626a87e12 at 09:53);
every comparison below pairs runs on the same content id (`run.json`).

**1. Urban sun shadows (owner: much softer and fainter in JKSunny) — cause found and fixed.**
- Not the shadow inputs. Logged at runtime (`r_sunShadowInfo 1`, SUNINFO lines): fov 80/50.5, splits 265 / 896 /
  2940 units radius, 0.26 / 0.87 / 2.87 units per texel at 2048, sun colour 1.064 1.021 0.957, overbright 0/0,
  identity light 1, colour scale 1, the same as rend2's code computes. Depth range, bias (none), clamp, comparison
  sampler and PCF match rend2; JKSunny's shadow-value view (`r_sunShadowDebug 2`) shows crisp, correct shadows
  (`scenes/20261009T083946-best-decided`).
- What differed: the sun's contribution on the street. Sun-only probe (`sunonly` profile: `r_forceSun 1`,
  `r_forceSunMapLightScale 0`, cubemaps off): walls 15 in both, sunlit street 41 (rend2) vs 9.4 (JKSunny). Lightmap
  part equal (shadowed street 55 vs 51).
- rend2 probe build (`src/somaz-r4dbg`, branch `debug/r4-lightviews`, `r_r4DebugView`; never a measured candidate)
  against JKSunny's probe views (`r_sunShadowDebug 5..12`): on the street rend2's "diffuse", N.L and N.sun were all
  saturated, as if several passes added up. The street faces carry lightmap styles 32/33/34
  (`siege_urban_pbr.bsp`). rend2's `CollapseStagesToGLSL` turns each style stage into a full copy of the lit diffuse
  stage with additive blending, so the sun (and the probe output) is added once per style: 4x on the street.
- Fix (perf/vulkan **b24362a11**, `r_sunStylePasses`, default 1 = rend2): the lit stage adds the sun 1 + (style
  count) times. The cubemap term is not repeated (rend2 scales it by the pass's light, the style light).
  **Superseded the same day (R5, below):** rend2's per-style sun is a rend2 bug; the sun is added once, in both
  renderers. (Correction: rend2 leaves cubemaps and dynamic lights out of style passes altogether.)
- Result, aligned urban start view, same content (`scenes/20261009T100936-best-decided`, pairs
  `pairs-20261009-r4/fix_siege_urban_pbr/`): sunlit street 56.1 -> 84.5 (rend2 89.4), shadowed street 51.1 (rend2
  55.0); mean luma of the view 86.6 -> 93.6 (rend2 96.2). The hover cart's and lamp's ground shadows now show as in
  rend2. Dantooine and Illimiran (`fix_ctf_*`) are unchanged in tone (their lit floors carry no styles).
- Before/after for the owner (one aligned frame, same content, quiet window):
  `pairs-20261009-r4/urban_start_rend2_before_after.jpg` (rend2 | JKSunny 8b679a8e5 | JKSunny 4b2a01cd2); per view
  `final_before_s8/` and `final_after_s9/` (start view mean luma 86.9 -> 93.6, rend2 96.2).

**2. Police car (open, narrowed).** Probes (`scenes/20261009T095116`-`20261009T103316-best-decided`): diffuse, L and N.L match; vertex colours match
(159 157 150 on the side in both); JKSunny's lightmap there is a flat 128 (lightmap 96), and rend2's car shader has
no lightmap at all (its lightmap probe view leaves the car unchanged), so rend2 lights it by vertex colour only
(0.76) where JKSunny multiplies by 0.5 (0.55). Next: find why rend2 drops the lightmap for these triangle-soup
misc_models (all 202 on lightmap 96, the last of 97), then copy that rule.

**3. Sun CPU cost.** `r_sunShadowInfo 2` prints SUNPROF (CPU ms per frame). Cyber: cascade front end 0.18 ms, cascade
command recording 0.25 ms (1720 cascade surfaces), main view front end 0.09 ms. Cascades now cull faces by the light
direction like rend2 (exactly the faces the flipped-cull pipelines would drop; identical images): uncapped, Cyber
337.6 -> 363.1 fps (2.96 -> 2.75 ms), Dantooine 385.1 -> 394.3 (`scenes/20261009T085842-best-decided-uncapped`, one
rep, dev build with SUNPROF on; sun off 490 / 443). Skipping the cascade draws entirely (`r_sunShadowDebug 4`) gives
2.45 ms on Cyber, so drawing them costs about 0.3 ms and the rest of the sun's 0.7 ms is the front end and shading.

**Pinned 4b2a01cd2** (`builds/jksunny-build-4b2a01cd2`, candidates `jksunny-vk-s9`, `-s9-sun-par`; cascade cull, style
passes, prepass). Quiet window, uncapped fps, owner preset + parallax, one rep, content 4a1626a87e12
(`scenes/20261009T104029-best-decided-uncapped`):

| map | rend2 perf | JKSunny 8b679a8e5 | JKSunny 4b2a01cd2 |
| --- | --- | --- | --- |
| siege_urban_pbr | 383.9 | 575.5 | 676.3 |
| ctf_dantooine_pbr_clean | 364.4 | 385.7 | 418.1 |
| ctf_illimiran_pbr | 446.9 | 546.7 | 553.5 |
| siege_urban_cyber | 348.6 | 343.4 | 340.7 |

Cyber does not gain: it is CPU-bound (GPU frame 1.89 ms, `scenes/20261009T104730-best-decided-gputimers`, against
2.9 ms per frame) and the prepass submits the opaque list a second time, which eats the cascade-cull saving there.
Next for Cyber: a cheaper prepass submission (depth-only draws without the per-stage descriptor work) or merging
depth-only cascade/prepass surfaces into one draw per VBO range, as rend2 does with `DEPTHPREPASS_SIMPLE`.

**4. Dantooine black wedges (fixed 2026-10-13 in xenjk rd-vulkan; see renderer-vulkan-in-xenjk.md "Dantooine black wedges").** They rotate with the weapon pickup on the courtyard pad (they move
between runs), and they are not: blob shadows (`cg_shadows 0`), flares, dynamic lights (`r_dynamiclight 0`,
`r_dlightMethod 0`), cubemaps, deluxe maps, normal or specular maps. They vanish in every shader probe view
(`r_sunShadowDebug 5/8`), so they come from the final shading of the floor (or the pickup itself), not from a
missing surface. Next: a probe view per term of the lit output (sun, lightmap, IBL, dlights) on Dantooine.

**5. Depth prepass** (perf/vulkan 415e55a33, on by default since **4b2a01cd2**, `r_depthPrepass`, rend2's default 1):
opaque world and model surfaces drawn depth-only with their own vertex shader before the lit pass. Images identical
on all four maps (`pairs-20261009-r4/prepass_*`, mean |diff| 0.05-1.6, animated billboards only). GPU timers at 4K
(`scenes/20261009T094109-best-decided-gputimers`, same content): main pass urban 0.989 -> 0.798 ms, Cyber 1.438 ->
1.210 ms; frame 1.657 -> 1.526 and 2.174 -> 1.941 ms. Parallax now costs about 0.05 ms over R3's no-parallax main
pass (0.752 ms), against rend2's 0.15.

**6. Ghoul2 bone cache:** exercised. The `npcs` profile now joins a team, spawns three NPCs in front of the urban
start view and returns to spectator (team changes are rate-limited). `scenes/20261009T102218-best-decided-npcs`: a
reborn with its saber renders on the cached path (dev build) with its sun shadow; no corruption. NPC AI moves them,
so frames are not comparable pixel for pixel between runs.

**7. Sky extras (open).** `r_flares` is 1 in JKSunny and 0 in rend2; turning it off does not remove Dantooine's
blue sky dots. The aligned Cyber pair (`fix_siege_urban_cyber/view_holo4_pair.jpg`) shows what JKSunny draws and
rend2 does not: a second hologram, a cyan window sign, more skyline, light beams. Not cheap; not done.

**Found on the way: `r_ssao 0` with MSAA loses the device** (VK_ERROR_DEVICE_LOST at the first frame) since
dc758321f (one MSAA resolve); s2 46950287c is fine, MSAA off is fine, glow off does not help. Not fixed; the bench
always runs with SSAO on. (2026-10-13: does not reproduce in xenjk's rd-vulkan, e7e2760e; see "Next session".)

## Sun once per surface: lightmap styles (2026-10-09, R5)

**Owner decision (2026-10-09):** the sun is added once per surface in both renderers; rend2's per-style sun is a bug,
not the reference. `r_sunStylePasses 1` keeps rend2's old behaviour for comparisons (profile `manysun`).

**Why rend2 is wrong here.**
- Styles are dimmable baked light layers (JK2/JKA RBSP: up to 4 lightmaps per surface). Urban's are the objective
  lights, the same in Raven's `siege_urban_v10.bsp`, `siege_urban_pbr.bsp` and `siege_urban_cyber.bsp`: style 32
  `barrelsdead` (light 100/300), 33/34 and 35/36 the green/red objective indicators (light 30), switched by
  `SP_light` -> `misc_lightstyle_set` (codemp/game/g_misc.c). The sun is baked into the style-0 lightmap only.
- Vanilla (rd-vanilla tr_shader.cpp ~2908) copies only the lightmap stage per style and adds it:
  `(sum of style x lightmap) x albedo`, each light once.
- rend2's `CollapseStagesToGLSL` copies the whole lit stage per style. The copies leave out cubemaps and dynamic
  lights (`rgbGen != CGEN_LIGHTMAPSTYLE`, tr_shade.cpp ~1897) but not the sun, and the sun term in lightall.glsl
  (`u_PrimaryLightColor`) is not scaled by the style colour: a surface with N styles got the sun N+1 times, and a
  switched-off light (style 'a') still added a full sun.

**JKSunny had a second problem** (`shaderlist styles`, new dev command, dumps every styled shader's final stages):
the urban `.mtr` materials list the texture before `$lightmap`, so the collapse multiplies texture x base lightmap
into one lit stage and the inserted style stages added their lightmaps after it, without the texture (the `.shader`
versions, `$lightmap` first, were fine and still take the vanilla path).

**Fixes:**
- JKSunny perf/vulkan **e5a36abc7**: `LitLightmapStyles` turns each style into a lit copy of the stage (that style's
  lightmap and colour, additive, no depth write). A style pass adds only its own light: no sun, sun shadows,
  cubemaps or dynamic lights. `r_sunStylePasses` now defaults to 0. Urban's road (styles 32/33/34) is drawn as base
  pass + 3 lit copies; the old shape was base + 2 plain lightmap passes.
- rend2 perf/rend2 **1e48808b4** (local): `u_StylePass` zeroes the sun in style passes; `r_sunStylePasses` default 0.
  Dev build `builds/somaz-prof-dev`, candidate `somaz-rend2-dev` (look only; not pinned).

**Results** (hidden runs, content 4a1626a87e12, `best,decided`; records and diff maps in
`.codex-research/renderer-bench/stylefix-20261009/`, comparison script `stylecmp.py` there):

| urban view | rend2 as is | rend2 once | JKSunny old (b24362a11) | JKSunny once |
| --- | --- | --- | --- | --- |
| start (mean luma) | 96.2 | 89.6 | 93.6 | 86.9 |
| obj1 | 94.9 | 87.8 | 94.1 | 86.9 |
| intermission | 130.3 | 121.7 | 129.3 | 120.6 |
| obj3 / obj4 | 98.0 / 116.0 | 97.9 / 116.0 | 97.6 / 115.6 | 97.3 / 115.3 |

- With `manysun` both renderers reproduce their old images (rend2: same means, differences within run noise;
  JKSunny: start 93.6, as b24362a11).
- Illimiran: unchanged in both. Dantooine: same means (differences are the rotating pickup). Cyber: changes where the
  objective lights reach (obj3 sidewalk: overbright in old JKSunny and rend2 as is, now close between the two;
  JKSunny obj3 52.8 -> 47.5, rend2 52.1 -> 50.8).
- **Seam study:** no seams on the bench views. Style coverage follows whole road and sidewalk brushes, so rend2's bug
  shows as a sun strength that varies by surface (road 4x, sidewalk 2x, walls, props and planks 1x under one sun), not
  as edges. The one candidate edge (obj1, the lane by the police car) is a building's shadow: JKSunny's shadow-value
  view (`r_sunShadowDebug 2`) ends the sun on the same diagonal.
- **Look:** with one sun the urban street is much flatter (its sun shadows were mostly the 4x sun). Owner sheet:
  `stylefix-20261009/FOR_OWNER_urban_sun_once.jpg` (rend2 as is | rend2 once | JKSunny once; start, obj1, obj4). If
  the owner wants the street's sun back, the uniform fix is a brighter `q3gl2_sun` in the urban pack (every sunlit
  surface alike), not per-style sun.
- **GPU cost** (quiet window 2, `best,decided,gputimers,hidden`, two reps, median ms per view, mean of views; script
  `stylefix-20261009/gpucost.py`; run `scenes/20261009T172237-...-gputimers-hidden` and `20261009T173806-...-stylesimple`;
  the aborted first launch left partial runs `172159`, `172210`). The hidden window does not skew GPU timers: old
  JKSunny urban main pass 0.776 / frame 1.498 against R4's visible 0.798 / 1.526.

  | arm | urban main / lit pass | urban frame | Cyber main / lit pass | Cyber frame |
  | --- | --- | --- | --- | --- |
  | JKSunny old (4b2a01cd2: sun x styles, plain style lightmaps) | 0.776 | 1.498 | 1.256 | 2.013 |
  | JKSunny sun once, full lit style copies (e5a36abc7) | 0.983 | 1.661 | 1.496 | 2.323 |
  | JKSunny sun once, simple copies (`r_stylePassSimple 1`, 69b5f9e12) | 0.906 | 1.584 | 1.337 | 2.094 |
  | rend2 as is (f26634fcb) | 1.144 | 1.658 | 1.234 | 1.857 |
  | rend2 sun once, no shadow lookup in style passes (9fff9577e) | 1.022 | 1.536 | 1.151 | 1.787 |

  - Full lit copies cost JKSunny +0.16 ms (urban) / +0.31 ms (Cyber) per frame at 4K; simple copies (no parallax,
    normal map or specular in style passes) +0.09 / +0.08. rend2 got cheaper: -0.12 / -0.07 ms.
  - Simple copies look the same as full ones (urban start mean |diff| 0.71, run noise; Cyber 0.4-3.1, rain): the
    objective lights are dim.
  - Owner 2026-10-09: simple copies are the default (d4090e931); the largest consistent differences (Cyber window
    trims, a bollard top) only show amplified 8x.
  - The way back to (or below) the old cost is one pass: the base pass reads the style lightmaps from the same atlas
    and adds them (extra lightmap texcoords for styled surfaces in the vertex path and shader variants).

**Hidden runs** (owner 2026-10-09: less disruptive): profile `hidden` (`"hide_window": true`) has the launcher move the
client window to y 12000, below every monitor, within ~0.1 s of it appearing (DPI-aware; `vid_ypos` alone left it at
0,0), and hand the foreground back; `in_nograb 1`. Screenshots are identical to visible runs (urban, all views, same
mean luma). Use it for every look-only run; not for timing.

## Capability review by category (2026-10-13, updated 2026-10-14)

Given in chat on 2026-10-13 from reading the rd-vulkan shaders and passes; this copy adds what landed since.

| Category | 2026-10-13 | 2026-10-14 |
| --- | --- | --- |
| Surface materials | Deep: GGX, both PBR workflows, normal + detail normals, parallax, skin, hair, cloth sheen, smoothed cutouts | Plus geometric specular AA and reflection occlusion from material AO (85ed982d). Ahead of rend2. |
| Animation / geometry | Deep: GPU cloth, G2 blend smoothing | Same; interrupted-blend pop deferred. |
| Sun light and shadows | Solid: three 2048 cascades, 9-tap filter, characters cast; no contact shadows | Large-caster scissor fixed (5abd5c28). Contact shadows optional, not scheduled. |
| Character shadows off the sun | None | Grid-derived shadow-only lights (`r_charShadows`, default 1 since 2026-10-14, e0329c59), floor-anchored under airborne casters (`r_charShadowFloor`, default 1). |
| Dynamic lights | Shallow: full material model, no shadows, every pixel loops every light | Unchanged. |
| Indirect light on characters | Weakest: one grid direction + flat ambient; probe diffuse commented out; nearest cubemap only | Irradiance shapes the ambient (`r_cubeMapDiffuse`), box-parallax probes blended per pixel (cef0129a). Only the xenjk packs place probes; other maps get one spawn-point probe. |
| Ambient occlusion | Mid, multiplied over the lit frame | Unchanged; SSAO still darkens sun and specular. |
| Image pipeline | 8-bit, no tone mapping (owner), bloom off, 4x MSAA, no temporal AA | Unchanged. |
| Atmosphere | Vanilla Q3 fog, flares, glow | Unchanged. |

Cheap list: 1 diffuse probe ambient (done), 2 reflection occlusion (done), 3 specular AA (done), 4 GGX multiple-scattering
energy fix (not built), 5 contact shadows (optional), 6 probe blending (done). Structural gaps still open: AO on ambient and
reflections only (needs characters in the depth prepass), dynamic-light shadows, float pipeline with tone mapping (owner
decision stands), dynamic-light culling, load-time probe placement for maps without `misc_cubemap`.

## Next session: JKSunny to full rend2 capability and look

Status on 2026-10-13 (supersedes the "Next" lists below; rd-vulkan now lives in xenjk, see
renderer-vulkan-in-xenjk.md; all of it is on `master` since 2701e2c3):
- **Closed:** Dantooine black wedges (patch-pole tangents, 6a111412, Install 6); 5f sun preset and parallax as the
  defaults (2f785a34); `r_presentWait` / `r_swapchainImages` kept as they are (owner); sun cascade large-caster
  scissor (5abd5c28); `r_ssao 0` with MSAA device loss, which no longer reproduces on the installed Install 9 build
  (e7e2760e; 4x MSAA, mp/ffa3 and siege_urban_pbr, glow on, bloom on and off, in game and under the team menu, all
  clean exits; `.codex-research/msaa-ssao-20261013/`, runner `msaa_repro.py`). Cause of the earlier loss not found.
- **Police car (fixed, fa4fc05b, Install 10):** rend2 lights a vertex-lit diffuse + `$lightmap` stage by the
  vertex colour alone (its last CollapseStagesToGLSL pass overwrites the collapsed USE_LIGHTMAP with
  USE_LIGHT_VERTEX); rd-vulkan now does the same on the lit path. See "Police car" in
  renderer-vulkan-in-xenjk.md.
- **Open:** none on the renderer list.
- **Optional, not scheduled (owner 2026-10-13):** contact shadows, Cyber CPU (depth-only submission), Cyber sky extras and Dantooine's blue sky dots,
  a one-step demo -> video tool.
- **Character shadows (owner 2026-10-14):** shadow-only lights over the baked lighting; map light survey and
  proposed order in [renderer-character-shadows.md](renderer-character-shadows.md). Stage 1 (grid-derived
  shadows, `r_charShadows`, default 1 since 2026-10-14) implemented and checked on ctf_kejim/ffa1, against Cycles, and on Eve with GPU cloth; e0329c59, Install 11.

State on 2026-10-09 17:15 (R5; "Sun once per surface: lightmap styles" above):
- **perf/vulkan head e5a36abc7** (lit style copies, sun once; not built pinned: the dev build equals it).
  **perf/rend2 head 1e48808b4** (sun once; dev build `somaz-prof-dev`). Pinned measured builds are unchanged
  (`jksunny-build-4b2a01cd2`, `somaz-prof-f26634fcb`): both still add the sun per style.
- Look-only runs use the `hidden` profile.
- **perf/vulkan d4090e931**: `r_stylePassSimple` default 1 (owner 2026-10-09, after the crop sheet
  `stylefix-20261009/FOR_OWNER_style_simple_vs_full.jpg`: "looks same to me"); the single-pass rewrite stays the
  upgrade if a map with strong style lights on bumpy or shiny surfaces shows the difference. **perf/rend2 9fff9577e**
  skips the shadow lookup in style passes. Style-copy GPU cost is in the section above.
- **Next:** the owner's look check of the sun-once urban street (sheet above; option: brighter urban `q3gl2_sun`);
  build both heads pinned before the next measured comparison. Then the list below.

State on 2026-10-09 10:55 (R4; results in "Urban sun shadows, aligned camera, depth prepass and probes"):

State on 2026-10-09 10:55 (R4; results in "Urban sun shadows, aligned camera, depth prepass and probes"):
- **perf/vulkan head 00dc6300c**; **4b2a01cd2 is built pinned** (`jksunny-build-4b2a01cd2`, candidates
  `jksunny-vk-s9`, `-s9-sun-par`). Done today: urban sun shadows (style passes), cascade face cull, depth prepass on
  by default, Ghoul2 bone cache exercised, aligned cameras (benchcam).
- **Dev build** `builds/jksunny-perf-dev` = the worktree; probe candidates `jksunny-vk-dev-sun-par-dbg5..12`,
  `-info` (SUNINFO), `-prof` (SUNPROF), `-pre`. rend2 probe build: `somaz-rend2-r4dbg*` (`src/somaz-r4dbg`, branch
  `debug/r4-lightviews`, `r_r4DebugView` 5-18; look only, never measured).
- **Screen locks (lock rules v2):** look-only runs take `screen/` without waiting for `laps/`; timing needs `quiet/`
  (file `quiet-request`). `.codex-research/renderer-bench/batches/r4/screen_run.sh <tag> <cmd>` takes and releases
  `screen/` safely; `q1.sh` is the quiet-window pattern.
- **Merged 2026-10-09:** the bench camera (`renderer/benchcam-20261009`) is in `movement/level1-gate` (56910513);
  map packs re-traced and approved by the owner (cf75461e, bench content 4a1626a87e12); `dist/` rebuilt.
  Owner decisions: parallax on in every comparison, `r_presentWait` kept at 0, rend2's per-style sun copied
  (`r_sunStylePasses` 1).
- **Next, in order:**
  1. Police car: find why rend2 draws the lightmap-96 triangle-soup misc_models without their lightmap (probe view
     17 leaves them unchanged) and copy the rule.
  2. Dantooine black wedges: probe each term of the lit output (they rotate with the weapon pickup; not blob
     shadows, flares, dlights, cubemaps, deluxe, normal or specular maps).
  3. `r_ssao 0` with MSAA: VK_ERROR_DEVICE_LOST since dc758321f (one MSAA resolve).
  4. Cyber CPU: cheaper depth-only submission for the prepass and the cascades (Cyber 340.7 fps vs rend2 348.6).
  5. Cyber sky extras (second hologram, skyline, beams) and Dantooine's blue sky dots (not `r_flares`).
  6. 5f defaults with the owner (sun preset, parallax); r_presentWait / r_swapchainImages stay as they are.

Earlier state (R3, 06:25; R2's handoff below, then today's results in "Parallax check and 5g parity pairs"):
- **8b679a8e5 is built pinned** (`jksunny-build-8b679a8e5`; candidates `jksunny-vk-s8`, `-s8-sun`, `-s8-sun-par`,
  `-s8-sun-pardbg`). Steps 1 (parallax check) and 4 (5g) of the list below are done; the numbers and the remaining
  visible differences are in the section above. Open from them: the black quads on Dantooine floors, the flare
  sprites in JKSunny's skies, Cyber's sky layers and main-pass cost, parallax cost 0.24 ms (no depth prepass), the
  sun's CPU cost, the police car, and the parallax/sun defaults (step 3, owner decides).
- **Ghoul2 bone cache still not exercised on screen:** the `npcs` profile (scenes.json; `pre_cmds` spawn four NPCs at
  the urban start view via `renderer_scenes.py run --maps siege_urban_pbr --candidates jksunny-vk-s7-sun,jksunny-vk-s8-sun
  --profile best,decided,npcs`) is written but untried: the `laps/` lock was held by night tests for 40+ minutes, so
  the screen window never came. Check: the NPCs spawn from a spectator, and the s8 (cache) frames show the same
  models as s7 (no cache; 13ed3f18f) with sun shadows on them.
- (R2 handoff, 2026-10-09 05:10:)
- **JKSunny** (`perf/vulkan` **8b679a8e5**): steps 1-4 done; latency step 2 done (settings, defaults off, owner's
  call); sun 5a-5d done (off by default); 5e: models cast, are sun-lit, bone cache per frame (c659f743f), misc_model
  brightness fixed (8b679a8e5), police car still greyer than rend2; colour scale on unlit stages done (8b679a8e5, not
  yet checked against rend2); parallax ported but unverified, behind `r_parallaxMapping` (default 0).
- **8b679a8e5 is not built as a pinned candidate yet.** The dev build (`builds/jksunny-perf-dev`) equals it;
  candidates `jksunny-vk-dev-sun`, `-sun-par` (`r_parallaxMapping 1`) and `-sun-pardbg` (2) run it. For measured
  runs, check 8b679a8e5 out in `src/jksunny-build`, `renderer_bench.py build jksunny-vk-build`, and add
  `jksunny-vk-s8` / `-s8-sun` pinned to `jksunny-build-8b679a8e5`.
- **Content:** the bench copy is the 2026-10-09 rebuild (manifest id 90f14a4543d5); baseline in the section above.
- **Next, in order:**
  1. Parallax check: one urban run of `jksunny-vk-dev-sun-pardbg` (green where parallax is active), then a crop pair
     against `somaz-rend2-perf` on the urban views (obj1 masonry) and `gputimers` for `-sun-par` vs `-sun` (target
     about 0.1 ms; rend2's costs 0.05-0.06 ms on Illimiran/Cyber). Then decide its default with the sun defaults.
  2. Police car: rend2 lights it about 0.76 (lightmap only, no sun) vs JKSunny 0.55; the lightmap texel there is 128,
     vertex colours mean 221. Suspects: `lightColor /= max(surfNL, 0.25)` with the per-vertex light direction, or how
     rend2's lightmap atlas samples lightmap 96 (a 4x4 block of 128 among zeros).
  3. 5f: make the owner's preset the JKSunny defaults once 5g passes (`r_sunlightMode 2`, `r_sunShadows 1`,
     `r_shadowMapSize 2048`, `r_shadowFilter 2`, `r_shadowCascadeZFar 4096`, `r_parallaxMapping 1`); rend2's own
     defaults are mode 1, shadows 1, 1024, filter 1, zfar 3072.
  4. 5g: parity pairs against rend2 on all four maps with `gputimers` (sheets per map as in `pairs-20261009-r2`),
     Dantooine included for the colour scale.
  5. The sun's CPU cost (above): JKSunny falls below rend2 on Cyber with the sun on.
- **Dev build:** `builds/jksunny-perf-dev` builds the worktree as it is (`cmake --build ... --target rd-vulkan_x86`
  in about a minute); candidates `jksunny-vk-dev-sun*` run it. Commit, then build the commit for measured runs.
- **Builds:** check a commit out in the detached worktree `src/jksunny-build` and run
  `renderer_bench.py build jksunny-vk-build`; add a candidate pinned with `"build": "jksunny-build-<rev>"`. Edits
  in `src/jksunny-perf` then cannot leak into a build in progress.
- **rend2** (`somaz-rend2-perf`, `perf/rend2` f26634fcb) is the visual reference.
- **Materials:** `urban_v2` is the material reference.
- **Screen time:** batches may run unattended (owner, 2026-10-09) under the `screen/` lock of the orchestration
  plan; keep each under about 15 minutes.
- **Profiles:** older JKSunny builds need `sunrewrite` (the q3gl2_sun rewrite moved out of `best`); `decided` is the
  owner's JKSunny defaults; `swap2` sets `r_swapchainImages 2`. A candidate's `"sets"` apply after the profiles.

Owner decisions (2026-10-09; the recommendations were accepted as a set):
- **Screen batches:** ask before each one. Later on 2026-10-09: short batches are fine unattended and while the owner is at the PC.
- **Later decisions (2026-10-09, relayed by the coordinator):**
  - `r_presentWait`: keep the feature, default stays 0; `r_swapchainImages` keeps its current default.
  - Parallax is on in every comparison: the `best` profile sets `r_parallaxMapping 1` for JKSunny (rend2's preset
    already has it). The engine default is decided at sign-off.
- **rend2:** make `r_maxFrameLatency 0` the default if its fps cost under GPU load is under about 5%.
- **JKSunny defaults:** bloom off; `r_hdr 0` with dither on; a 2-image swapchain behind a setting.
- **Sun port:** the answers are in [renderer-vulkan-sun-port.md](renderer-vulkan-sun-port.md).
- **urban_v2 plaster:** the owner judges the screenshot pairs.
- **Map pipeline:**
  - Fold `tools/pbr_heights.py` into the build pipeline (`<newmod tree>/rend2_comparison/tools/build_urban.py`)
    and rebuild the installed `zz_xen_rend2_urban.pk3`, keeping the old pack as a backup.
  - Then Dantooine, Illimiran and Cyber, one map at a time with screenshot pairs.
- **Tone mapping and exposure:** stay off; step 7 is not needed unless that changes.
- **Final renderer choice:** decided at parity sign-off, with numbers.
- **`perf/rend2`:** stays local, not offered upstream.
- **PresentMon:** not needed.

Steps, in order. Measure each with the cheapest check: `gputimers` for the GPU per pass, a scene pair for the look,
the latency probe for latency.

1. **Done 2026-10-09 (40d176ac0).** **Robust material parser (small).** An unknown general keyword skips the rest of its line instead of rejecting
   the material (V tr_shader.cpp 2968); parse `q3gl2_sun` fully (see 5a). Then drop the bench's
   `material_rewrites` for vulkan-pbr.
2. **Done 2026-10-09 (46950287c, 3774a3836; both settings off by default, see above).** **Presentation queue (latency).** Uncapped, about 1 in 5 samples still waits about 36 ms after the fence fix.
   - Try a 2-image swapchain for IMMEDIATE/MAILBOX (latched `r_swapchainImages`, vk_swapchain.cpp 149-152).
   - Then `VK_KHR_present_wait` on present N-latency.
   - Check: latency uncapped and at the 240 cap. Target: no 36 ms mode; capped median at or below rend2's.
3. **Done 2026-10-09 (dc758321f).** **One MSAA resolve (speed, no look change).** Composite SSAO, glow and bloom after a single resolve, at 1x, and
   draw the HUD at 1x. Today SSAO blend, dglow blend and bloom blend each reload and resolve the 4x MSAA target:
   0.94 ms together (vk_renderpass.cpp 755/380/495; blend pipelines use vkSamples in vk_pipelines.cpp ~1548-1617).
4. **Done 2026-10-09 (65f82206c).** **Cheaper SSAO (speed).** It is 1.24 ms against rend2's 0.30 ms. Use a half-res extract and a separable bilateral
   blur in place of the 5x5 (ssao_blur.frag:49, radius at vk_ssao.cpp:134), and add a radius cvar.
   - **Also in this step:** an 8-bit glow target, and wire `r_DynamicGlowScale` (it is defined but nothing reads
     it).
   - **Defaults:** decide with the owner on `r_bloom` (rend2 has none) and `r_hdr` (16-bit UNORM: precision only).
     Measured fps: 278 as is, 330 without bloom, 377 without hdr, 439 without both.
5. **Sun port:** [renderer-vulkan-sun-port.md](renderer-vulkan-sun-port.md), stages a-g; the owner's answers are
   in. (a)-(d) done 2026-10-09 (cf5aec729, e2a371813, 13ed3f18f); (e) done except the police car's remaining gap
   (c659f743f bone cache, 8b679a8e5 misc_model collapse). Next: (f) and (g).
   - (a) parse and scene values
   - (b) sun term, no shadows
   - (c) cascaded shadow maps (rendered before the main pass, like the screenmap pre-pass)
   - (d) PCF with a comparison sampler
   - (e) models lit and casting
   - (f) cvars
   - (g) parity pairs against rend2
6. **Parallax (look).** Ported 2026-10-09 (8b679a8e5, `r_parallaxMapping`, off by default); verification pending. Port rend2's `RayIntersectDisplaceMap` in its fast form (`r_parallaxFast`: limited by the
   on-screen shift, with steps scaled to it) into gen_frag.
   - JKSunny already parses `normalHeightMap`, `parallaxDepth` and `parallaxBias` (V tr_shader.cpp ~1692-1703);
     it needs the height in the normal texture's alpha flipped to depth, as rend2 does at tr_image.cpp:3070.
   - Also: the non-square-texture correction from rend2's velocity.glsl; the depth prepass, if added, applies
     parallax only for alpha test.
   - Check: obj1 crop pair against rend2 with `urban_v2`; GPU cost about 0.1 ms there.
7. **Tone mapping and exposure (look).** Only if the owner turns rend2's `r_hdr`/`r_toneMap` on; today's preset has
   them off. Then port rend2's tonemap and auto-exposure (tr_postprocess.cpp 43-84) into the 1x composite of step 3.
8. **Remaining rend2 features** (not used by today's maps; port when content needs them): point-light shadow maps,
   projected model shadows (`r_pshadow*`), sun rays, cubemap bounces, `refractive`/`cloth`/`rgbGen vertexLit`.
9. **Parity sign-off.**
   - Scene bench with matched settings, `--reps 3`, all four maps; contact sheets against rend2.
   - Latency probe at the 240 cap and uncapped; GPU timers.
   - Record the result in this doc.
   - The open renderer decision: port JKSunny's PBR Vulkan into xenjk, or take TaystJK's Vulkan plus SomaZ rend2.

**Open checks that don't block the steps:**
- Done 2026-10-09: rend2 `r_maxFrameLatency 0` costs 25% fps under GPU load, so the default stays 1.
- The look of the `urban_v2` plaster and stone door trims, flatter without the old noise parallax: stronger normal
  maps, if the owner wants the bumpiness back.
- `tools/pbr_heights.py` weak spots listed in its README (wall15 head joints, tracks dirt, grass shimmer): tracks
  fixed 2026-10-09, the other two open (see "Map pipeline").
- JKSunny's startup crash: solved 2026-10-09 (console echo with an uninitialized buffer info; see above).
- `r_presentWait` and `r_swapchainImages` defaults: the owner's call (numbers above).

## Map pipeline (2026-10-09)

The owner's map-pipeline decision is done for all four maps. The packs are rebuilt and installed in
`<newmod tree>/rend2/GameData/base`; the owner judges the look from the screenshot pairs.

**Pipeline:** `tools/pbr_heights.py` is a module the rend2 map builders in `<newmod tree>/rend2_comparison/tools`
import (`v2_maps` / `height_maps`, one table per map). A plain rebuild now writes structure-aware parallax
materials: a full-range height in the normal alpha where the relief is real, a plain RGB `_n` normal elsewhere.
`build_urban.py`'s output is byte-identical to the `urban_v2` override (72 files and the `.mtr`) apart from one fix:
tracks `slot_line` 41 -> 121 (dirt blotches). The unchanged builders reproduced every installed pack exactly first,
so each pack differs only in the intended materials.

| map | builder | parallax before -> after | installed pack (bytes) |
| --- | --- | --- | --- |
| siege_urban_pbr | build_urban.py (URBAN_V2) | 52 -> 20; 32 to plain normals | 156,429,328 -> 141,574,193 |
| ctf_dantooine_pbr(_clean) | build_dantooine.py (DANTOONINE_V2) | 9 -> 5; marble, plaster, frame panel, grimy wall to normals | dantooine 28,057,029 -> 26,315,242; showcase 103,318,529 -> 101,576,742 |
| ctf_illimiran_pbr | build_ctf_pbr.py `--heights illimiran` | 0 -> 5 (wall1 blocks, new7 grating, three seam panels) | 84,528,348 -> 84,447,726 |
| siege_urban_cyber | build_cyber.py assemble (CYBER_V2) | 0 -> 4 (CP brick/cinderblock walls; CP normals kept, traced height in alpha) | 235,696,525 -> 243,701,911 |

- **Showcase pack:** `zz_xen_rend2_showcase.pk3` carries the Dantooine materials too and sorts after the dantooine
  pack, so it was rebuilt as well.
- **Backups and records:** old packs in `<newmod tree>/rend2_comparison/backups/20261009/`; edited builders keep
  `*.bak-20261009`. Hashes are in `.codex-research/orchestration-20261009/status.md`. `dist/` split packs are
  still the old build.
- **New method options:** groove `normalise: raw` (stained stone), rubble `polarity: dark`, and plateau `grid: n`
  (exact cell grids). Dantooine's templewall_1 has no traceable joints and stays a normal map.

**Screenshot pairs for the owner:** `.codex-research/renderer-bench/maps-20261009/<map>/`. `somaz-rend2-perf` and
`jksunny-pbr-perf` (`best` profile; JKSunny draws no parallax), from the profiles `new_urban`, `new_dantooine`,
`new_illimiran` and `new_cyber` (`scenes.json`). Per view: `_pair.jpg` (old left, new right), `_crop.jpg` (1280x720
at full resolution, old above new, where they differ most), and `_sheet.jpg` per renderer. Cyber's auto crops land
on rain; its brick walls are dark at night. The superseded urban pairs (before the tracks fix) are in
`siege_urban_pbr_v2a-superseded/`.

**Not done:**
- The `urban_v2` override pack (the renderer work's material reference) still has the old tracks.
- wall15 head joints: no straightforward fix.
- Grass shimmer: needs an in-game look.
- GPU cost of the new parallax in rend2: measured 2026-10-09, lit pass +0.06 ms (Illimiran), +0.05 ms (Cyber).

**Trace fix (2026-10-09, later): the new urban wall did not look like its albedo.** Cause: `wall3` (masonry, regular
brick period, 4 px joints, depth 0.016) against an albedo with thin 3 px pale joints and small irregular stones: course
joints matched, most head joints were missed, a few sat on no real joint, and the joints were drawn wider than the
albedo's and deep (16 texels), so they rendered as heavy dark grooves.
- **Check built into the tool:** `python tools/pbr_heights.py check DIR --out OUT [--before OLD_OUT]` (albedos captured
  with `PBR_CAPTURE_DIR` while a builder runs) overlays the traced joints on every traced albedo at full resolution and
  scores the alignment per direction (strong albedo joints covered, traced centreline with albedo evidence, joint width
  albedo vs trace, against a shifted-trace baseline). Details in the module docstring.
- **Trace changes:** `heads: "albedo"` reads head joints per course from the albedo's own vertical edges instead of a
  brick period; `mortar` is the measured joint width; `depth` is at most 2 x the drawn joint width; wall15 and
  wall15door (alignment 0.3-0.4 at every setting) are plain normal maps; crate4 gets its cross seams.
- **Rebuilt and installed** (urban, Cyber; Dantooine and Illimiran traces unchanged): backup of the then-installed
  packs in `<newmod tree>/rend2_comparison/backups/20261009-trace/`. wall3 vertical coverage 0.65 -> 0.96, joint width
  5 -> 3 px (albedo 3), depth 0.016 -> 0.006.
- **Pairs for the owner** (morning backup packs vs fixed, `somaz-rend2-perf`): `.codex-research/renderer-bench/maps-20261009-trace/FOR_OWNER/`
  (one view per map, the obj1 wall close-up, the before/after overlay sheet). New `map_pairs.py --zoom X,Y,W,H`.

## Timedemo bench fix (2026-10-09, N2)

`renderer_bench.py` read the console log and screenshots from `home/base/`, but TaystJK and JKSunny write them into their
own game directory (`taystjk`, `EternalJK`). The runner therefore never saw their timedemo summary, waited the 180 s
stall limit per run and `report` left their rows blank. `home_file()` now takes the newest `home/*/<file>`. First
default run set (kejim-runner, 1080p uncapped, 3 reps): JKSunny 762/722 fps (default/max), SomaZ rend2 727/661, TaystJK
Vulkan 643/615, TaystJK rend2 561/532; table in `docs/movement/records/2026-10-09-night-items-3-5.md`.
