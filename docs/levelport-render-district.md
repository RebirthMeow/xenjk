# Level-port renderer at district size (phase 3, 2026-10-31)

Phase 3 of the CP2077 Watson port ([levelport-watson.md](levelport-watson.md) sections 4 and 15): the renderer at
district size. This page records the first three steps, measured before changed: where the stress slice's worst long
view spends its frame, the renderer's plane copy (remapped and gated), and why the 64-bit client commits about 3 GB
more than the 32-bit one. The render scene design is in [levelport-scene.md](levelport-scene.md).

Evidence, scripts and logs: `.codex-research/levelport/look/watson-p3-20261031/` (local only, like every converted
map). Builds: `%LOCALAPPDATA%/xenjk_build_p3_{a64,b64,b32}` (rd-vulkan only; A is `wt-install-20261031`, 5a03a725,
whose rd-vulkan is HEAD's; B is the worktree `.codex-research/wt-p3-remap`, HEAD 620f63fb plus the remap).

**In short.**

- **The worst long view is GPU-bound geometry, not culling.** East to west the slice takes 16.7-16.8 ms of GPU
  (59-60 fps) against Kabuki's 9.8-10.6 ms (95-102 fps). Of the 6-7 ms difference, about 4.8 ms is vertex and primitive
  work in the three passes that draw the scene's geometry (depth prepass, lit pass, sun cascades), 1.6 ms the lit pass's
  light loop (four times the lights), 0.4 ms the light clusters. The per-instance cull over every instance and view
  (vk_scene.cpp:3651) is **0.15-0.17 ms**: rejecting tiles first can save at most that on the slice.
- **Recommended first change:** occlusion culling of scene instances in `scene_cull.comp` against a depth pyramid
  built from the previous frame (eye view, so the prepass and the lit pass), measured first with a counter in the
  same shader. Second, cache the middle sun cascade's static casters (it and the far cascade cost 2.0 ms of this view).
  Tile rejection belongs with the per-tile scene files (it matters at Watson's 1.6 M instances, about 1 ms), not as
  the frame-rate fix.
- **Occlusion share, measured (section 1a):** 99.3-99.9% of the eye view's indices are hidden on the slice's three
  views and 91-98% on Kabuki's. Leaving them out takes the slice from 12.1-17.0 ms to 5.3-9.5 ms and Kabuki from
  9.6-10.1 ms to 5.9-7.0 ms, images within their noise. The slice's three "long views" face walls a few metres away.
- **Occlusion culling: built, gated, committed (2873dbc8, section 1b), not installed.** Two phases (last frame's depth,
  then this frame's prepass depth for what that hid: no pops), 0.24 ms a frame; every gate passed in round 2: down
  real streets the slice saves 0.5-5.1 ms, Kabuki 0.05-3.6 ms, Kabuki's 4K lap +4.2%.
- **Plane remap: done and gated.** Kabuki's renderer copy drops 87.8 MB and the slice's 213.0 MB, the predicted amount
  to the byte; images within each map's run-to-run noise on both builds.
- **Tiled scenes load (section 1c): committed, not installed.** The loader joins a `.xman` manifest's files into
  one scene in memory; the slice's tiled scene matches the single file in counts, images (camera locked) and timing.
  Watson (181 files, 1.6 M instances, 41 k lights) loads in 50 s and runs its street views at 14.9-17.0 ms (59-67 fps)
  with occlusion on. The haze pass and the light clusters lead there (4.3-4.7 and 2.2 ms).
- **Watson at 4K, 64-bit (section 1e):** street views across Little China, Kabuki and Northside 11.3-16.7 ms
  (60-88 fps) held still; a walking-pace lap 13.2-13.5 ms median, 16.0-16.5 p95 (scene only). Haze and light
  clusters take 6.2-6.8 ms of every frame; without them the streets would be 4.5-10.5 ms. Per-LOD visible ranges
  committed (a32d66cf): Watson VRAM -108 MB, images and timing unchanged.
- **The haze's cost is its volumes (section 1f):** Watson has 606, past the shader's 256-volume group mask, so all
  2.07 M froxels test every one: 4.1-4.3 ms. Lifting the cap should take its streets to about 7.2-12.5 ms.
- **The 64-bit +3 GB is the NVIDIA driver**, which commits write-combined process memory equal to every device-local
  allocation in a 64-bit process: the client's VRAM counted again as commit charge, never resident.

## 1. Where the long view's frame goes

**How it was measured** (`prof_views.py`, `prof_chain2.sh` / `prof_chain3.sh`). The owner's openjk.cfg at 3840x2160,
vsync and the frame cap off, first person; the 64-bit build of 3b14e1c2 with 5a03a725's rd-vulkan (HEAD's); slice-20
(map `cp_watson_slice-20`, scene `cp_watson_slice_scene-20`: 275,447 instances, 8,879 lights) against Kabuki
(Install 47's map-22 + scene-23: 65,569 instances, 2,154 lights). Phase M's three long views (`slice_views.py`'s
positions), each on one load: 600 frames of `r_gpuTimes 1` (per-pass GPU time), 600 of `r_cpuTimes 1` (the wall frame
and its parts), 300 of `r_sceneInvocations 1` (the eye's lit pass: vertex and fragment shader invocations, primitives
after clipping), then 300 frames with one cvar changed at a time and a closing base window (drift). The first tenth of
each window is dropped. Every run held `GAME_LOCK_EXCLUSIVE` (no other client) after a quiet minute (CPU under 30%, no
q3map2, WolvenKit, blender or levelcheck); packs read from C:. Slice, Kabuki, then both again without the ablations.
No Nsight on this machine; the ablations stand in for it.

**Frame time** (GPU median / p95 / p99 in ms; CPU = the wall frame from `r_cpuTimes`):

| View | Slice GPU, run 1 / 2 | Slice CPU | Kabuki GPU, run 1 / 2 | Kabuki CPU, run 2 |
|---|---|---|---|---|
| East to west | 16.70 / 18.76 / 20.05; 16.83 / 17.98 / 18.54 | 17.80 / 19.01 / 19.70 | 10.55 / 11.57 / 12.49; 9.76 / 10.26 / 12.06 | 10.67 / 11.21 / 11.51 |
| South to north | 14.58 / 15.09 / 15.42; 14.57 / 15.25 / 15.46 | 15.83 / 16.41 / 17.06 | 9.91 / 11.62 / 14.04; 9.33 / 9.94 / 10.26 | 10.33 / 10.95 / 11.18 |
| West to east | 11.87 / 12.34 / 12.66; 11.90 / 17.27 / 18.89 | 13.39 / 14.21 / 15.07 | 9.55 / 10.38 / 10.78; 9.48 / 10.03 / 10.22 | 10.36 / 11.62 / 20.74 |

Phase M's slice-1 numbers repeat (east to west 16.15 ms then). Both maps are GPU-bound: of the slice's 17.8 ms wall
frame the CPU waits 15.5 ms for the GPU (`latency`); the front end takes 1.1 ms (Kabuki 0.15-0.2), the backend's
recording 0.55. Kabuki's first run had 31-36 frames a window over 20 ms, all of it waiting on the GPU while our own
passes measured 12-14 ms at p99: another program on the GPU; its second run had none. Kabuki itself runs these views
at 95-107 fps, short of 120 too; the slice is 59-84.

**Where the slice's east-to-west frame goes** (GPU ms, medians of the two runs; Kabuki from its clean second run):

| Pass | Slice | Kabuki | Slice - Kabuki | What it is |
|---|---|---|---|---|
| Lit pass (`scene`) | 6.34 | 3.13 | +3.2 | the eye's opaque, cutout and emissive lists, depth-equal after the prepass |
| Depth prepass (`scene depth`) | 3.79 | 2.27 | +1.5 | the same instances, depth only |
| Sun cascades (`scene shadow`) | 3.29 | 1.55 | +1.7 | the scene's casters in three 4096 cascades |
| Light clusters, list packing | 0.60 | 0.20 | +0.4 | 40 x 24 x 32 clusters against every scene light; the lists |
| Haze | 0.38 | 0.18 | +0.2 | the froxel volume |
| Instance cull (all views) | 0.16 | 0.04 | +0.1 | `scene_cull.comp`, every instance per view |
| Everything without the scene (`r_scene 0`) | 1.9 | 1.5 | | the BSP's sky, post, SSAO, grade, HUD |

**Vertex work, not pixels** (the eye's lit pass, `r_sceneInvocations`): the slice runs 27.3 M vertex shader invocations
and 10.8 M primitives after clipping for 8.5 M fragment invocations (1.03 a pixel: the depth-equal test leaves no
overdraw to shade); Kabuki 11.2 M, 7.5 M and 9.0 M. The same pixel count is shaded; the slice pushes 2.4 times the
vertices through each of the geometry passes. The other two long views say the same (28.6 M and 30.3 M against
10.8-11.3 M).

**One thing changed at a time** (east to west, frame ms against the base window's 16.7; Kabuki's in brackets, from its
first run, base 10.55; window-to-window noise about 0.3 ms on the slice, 0.5 on Kabuki):

| Change | Slice | What it shows |
|---|---|---|
| `r_sceneShadows 0` (no scene casters in the sun cascades) | 13.15 (8.25) | the cascades cost 3.5 ms here |
| `r_sceneShadowCascades 1` (the near cascade only) | 14.94 (9.03) | the middle and far cascades 2.0 ms of it |
| `r_sceneLightScale 0` (every light's loop exits early) | 15.00 (9.77) | the light loop: 1.9 ms (Kabuki 0.3) |
| `r_sceneDebug 1` (albedo, no lighting; a debug pipeline) | 16.32 | no gain: the debug pipeline's own cost cancels it |
| `r_sceneLodBias 0.5` (coarser LODs) / 2 (finer) | 15.07 / 19.09 | geometry scales: -1.6 / +2.4 ms over the three passes |
| `r_sceneCullPixels 6` (small instances dropped) | 15.93 | -0.8 ms: small, distant placements |
| `r_sceneClusters 0` (the light grid's cells) | 16.61 | same here; west to east +1.8 ms: keep the clusters |
| `r_sceneLightShadows 0` (lamp shadow maps not read) | 16.62 | none: the lamp shadows are free at draw time |
| `r_sceneReflect 0`, `r_sceneFogLight 0`, `r_sceneGlow 0` | 16.39, 16.33, 17.37 | 0.3-0.4 ms or none |

**What this says.**

- The per-instance cull is not where the time goes: 0.15-0.17 ms for all of the slice's views (275 k instances), 0.04
  on Kabuki (66 k): linear in instances, so about 1 ms at Watson's 1.6 M. Tile rejection is worth having when the
  scene becomes per-tile files, for that reason, but it cannot recover the slice's 7 ms.
- Geometry is: the prepass, the lit pass's vertex share and the cascades together carry about 4.8 ms of the slice's
  excess, and they scale with what is drawn (LOD bias, the small-instance cut). Shading is not: the lit pass shades the
  same 8.5-9 M fragments on both maps, and lighting-free shading does not speed it up.
- The light loop is the second cost: 1.9 ms on the slice against 0.3 on Kabuki, with 4.1 times the lights (the
  clusters keep it bounded; their per-cluster lists are long downtown).
- The cascades are the third: 3.3-3.5 ms in this view (1.2 and 0.8 ms in the others: the sun's direction against the
  street), 2.0 ms of it the middle and far cascades.

**Recommended first change: occlusion culling in the instance cull.** Each frame, build a depth pyramid from the eye
view's prepass and, in `scene_cull.comp`, reject instances whose bounding sphere lies behind the previous frame's
pyramid (reprojected; a second phase re-tests the rejected ones against this frame's pyramid so nothing pops in). It
attacks the largest bucket (the prepass and the lit pass, 10.1 ms of the 16.7, both vertex-bound), costs no image
quality, sits in a pass that today costs 0.16 ms, and is what long streets need at district size (Watson's views will
reach past 277 m once the far plane is decided). It does not help the cascades. **Measure before building it**: add a
counter to the cull shader that, without changing what is drawn, counts eye-view instances and their vertices a
last-frame pyramid would reject on these three views; build it if that share is large (the slice pushes 3.2 vertices
a pixel against Kabuki's 1.2, which suggests it is). Next in line, by the same data: the middle cascade's static
casters cached like the far one's (`r_sunShadowFarCache`), up to 2 ms in this view; shorter per-cluster light lists
(light reach against the cluster, a cap), up to 1.6 ms; the game's level 4-6 proxies when the far plane moves past the
level 0-2 bands.

## 1a. The occlusion share, measured (2026-10-31)

Owner, 2026-10-31: "ok yes to both" (commit the remap, then measure the occlusion share). A measurement build, not a
change to the renderer: `r_sceneOcclusion` in `.codex-research/wt-p3-remap` (4d212187 plus
`occ/occlusion_counter.patch` and `occ/scene_occ.comp` in the evidence folder; not committed). After the reflections'
capture, `scene_occ.comp` builds a far-depth pyramid from the frame's depth extract (level 0 half the screen, each texel
the farthest linear depth of the texels it covers; sky infinitely far). In the next frame, `scene_cull.comp` tests each
eye-view instance that passed the frustum and size cuts: its bounding sphere's screen rectangle (the eight corners of
its box through the pyramid's camera) against the farthest depth over that rectangle, at the smallest level where it
spans at most 2 x 2 texels; hidden when the sphere's nearest point is farther. `r_sceneOcclusion 1` counts the hidden
instances and their LOD's indices (`SCENEOCC`); `2` also leaves them out of the eye view's lists (the prepass and the lit
pass; the sun cascades are culled separately and keep every caster). The camera is still in these windows, so last
frame's pyramid is this frame's depth: this is the bound a perfect occlusion cull reaches on these views, not a moving
camera's (that needs reprojection and a second phase for disocclusion).

Run: `occ/occ_views.py` under one `GAME_LOCK_EXCLUSIVE` after a quiet minute (the collision session told first), the
same three long views, 400 frames a window, the 64-bit build, 3840x2160, the owner's openjk.cfg; per view a base window,
`r_sceneOcclusion 1`, `2`, and the base again; the lit pass's invocations with it off and at 2; a screenshot with it off
before, at 2, and off again after (`occ_slice.jpg`, `occ_kabuki.jpg`).

| View | Eye instances, hidden | Indices, hidden | GPU frame: base, base again / mode 2 | Lit pass | Prepass | Lit-pass VS invocations |
|---|---|---|---|---|---|---|
| Slice east to west | 157,000, 99.9% | 57.2 M, 99.3% | 17.01, 16.95 / **9.50** | 6.32 -> 2.59 | 3.85 -> 0.33 | 27.3 M -> 0.14 M |
| Slice south to north | 173,992, 100.0% | 59.9 M, 99.9% | 14.91, 15.23 / **7.90** | 6.10 -> 2.22 | 3.59 -> 0.14 | 28.6 M -> 0.02 M |
| Slice west to east | 186,880, 100.0% | 62.6 M, 99.9% | 12.06, 12.21 / **5.33** | 4.39 -> 0.82 | 3.45 -> 0.14 | 30.3 M -> 0.02 M |
| Kabuki east to west | 49,035, 98.1% | 24.2 M, 91.0% | 10.08, 10.13 / **6.96** | 3.17 -> 1.24 | 2.20 -> 0.63 | 11.2 M -> 0.76 M |
| Kabuki south to north | 45,633, 99.8% | 23.5 M, 98.1% | 9.63, 9.66 / **5.94** | 2.90 -> 0.73 | 2.22 -> 0.34 | 10.9 M -> 0.14 M |
| Kabuki west to east | 50,265, 98.1% | 23.9 M, 92.5% | 9.86, 9.83 / **6.66** | 3.40 -> 1.62 | 2.35 -> 0.58 | 11.3 M -> 0.70 M |

(GPU ms, medians; p95 within 0.3-0.8 ms of each median. Sun cascades unchanged: 3.4, 1.2, 0.7 ms on the slice, 1.2-1.8 on
Kabuki. Fragment invocations unchanged in every view: the same pixels are shaded.)

- **Images.** With the hidden instances left out, each view matches its own screenshot with them drawn about as well as
  two screenshots with them drawn match each other (PSNR and pixels off by more than 16: slice 34.2 / 1.47% against
  34.0 / 1.57%, 40.4 / 0.37% against 40.2 / 0.37%, 25.8 / 28.4% against 14.6 / 32.4%; Kabuki 33.9 / 0.82% against
  34.4 / 0.56%, 27.1 / 3.2% against 28.5 / 1.8%, 35.0 / 0.45% against 34.7 / 0.48%). The differences are the wet
  street's reflections, the saber and its light, and the haze, which change between any two frames; no hole shows on
  the contact sheets. The owner's openjk.cfg plays third person despite the run's `cg_thirdPerson 0`.
- **The test's own cost** (mode 1 against the base): the pyramid 0.08-0.09 ms, the cull +0.05 ms; 0.1-0.3 ms a frame
  in all.
- **The slice's "long views" are not long.** Phase M placed them at the slice's edges looking across it, but from those
  positions all three face a wall or a facade a few metres away (`occ_slice.jpg`). They were slow because everything
  in the frustum within 277 m behind those walls was drawn: 57-63 M indices for a few hundred visible instances. Kabuki's
  views look down real streets and still hide 91-98% of what they draw.
- **What it means for the recommendation.** Occlusion culling is the change: on the slice's worst view it bounds at
  -7.5 ms (17.0 to 9.5), on Kabuki's -3.1 to -3.7 ms, both past the 8.3 ms target except the slice's east-to-west view,
  which is then 3.4 ms of sun cascades, 2.6 of shading and the rest. A moving camera will keep less than this bound
  (disocclusion re-tests, conservative reprojection), so the real figure is for the implementation to measure. The
  cascades are next (3.4 ms on that view): they draw casters the eye never sees, which eye occlusion cannot remove.
- **Before building the real thing**: phase M's view positions should be checked against what the camera sees, and a
  real long view added (down a street, sight line to the far plane), so the gate measures what the owner will play.

## 1b. Occlusion culling: street views and gates (2026-10-31)

Owner, 2026-10-31: "Yes, build it". Order set with it: street views first and their occlusion share, then a
moving-camera design (last frame's pyramid, then a second phase for what it hid) behind a cvar that defaults on only
once it passes; gates written down before they run; report before any install; commit renderer files only.

**Street views** (`occ/find_sightlines.py`, `occ/street_views_final.json`). Every road-mesh placement (lanes, medians,
curbs, fences along the road splines, about every 5.5 m) marks a 4 m grid; rays from each road cell in 72 directions
run along the road; the longest runs inside each converted scene, apart in place and aim, were looked at in game (one
screenshot each, `look_*.jpg`, `look2_*.jpg`) and the ones that look down a street kept. Kept: the slice's elevated
road (358 m of road ahead), `street_b` .. `street_h` (272-334 m); Kabuki's market ring (along the ring from the lap's
route) and four streets (132-218 m). Dropped: views outside the converted scene (void), into walls, and both canal
candidates (from the slice's water patches: they put the camera under structures). No canal view yet; Kabuki's scene
has no water at all (built before the water was converted).

**Their occlusion share** (the measurement build, `r_sceneOcclusion 1` counting only, one screenshot's worth of frames):

| View | Sightline | Eye instances, hidden | Indices, hidden |
|---|---|---|---|
| Slice elevated | 358 m | 46,348, 91.5% | 15.7 M, 83.7% |
| Slice street_b | 334 m | 16,453, 94.0% | 6.5 M, 83.5% |
| Slice street_c | 314 m | 80,985, 88.6% | 27.5 M, 76.8% |
| Slice street_d | 314 m | 174,214, 96.8% | 59.1 M, 93.0% |
| Slice street_e | 306 m | 32,646, 84.7% | 12.1 M, 73.5% |
| Slice street_g | 278 m | 33,793, 86.8% | 14.6 M, 76.2% |
| Slice street_h | 272 m | 116,241, 95.1% | 50.8 M, 82.1% |
| Kabuki street_a | 218 m | 28,728, 84.3% | 15.2 M, 66.9% |
| Kabuki ring | (curved) | 12,531, 68.6% | 7.7 M, 49.5% |
| Kabuki street_g | 156 m | 22,921, 90.1% | 11.6 M, 73.8% |
| Kabuki street_h | 142 m | 9,171, 72.9% | 6.9 M, 49.3% |
| Kabuki street_i | 132 m | 4,746, 80.2% | 4.1 M, 59.4% |

Down real streets three quarters of the slice's drawn indices and half to three quarters of Kabuki's are hidden: less
than the wall-facing views' 99%, still most of the geometry.

**Gates, fixed before running** (the implementation behind `r_sceneOcclusion`, 0 until they pass):

1. **Still views, images.** Every view above plus the three earlier ones on each map: a screenshot with occlusion off,
   one with it on, one off again. On against off must sit in the off-against-off noise: the share of pixels off by more
   than 16 at most 1.5 times the off-against-off share plus 0.1 percentage point, and no 8-connected patch of 400 or more
   pixels off by more than 32 in on-against-off that is not also off in off-against-off (dilated 3 pixels): a missing
   object leaves such a patch.
2. **Moving camera, images and pops.** Kabuki's lap demo (timedemo: the camera repeats exactly) at 1920x1080, a shot
   every 4th demo frame over three stretches of 120 frames (a turn, a straight, the ring's far side): 90 shots with
   occlusion off, twice, and on, once. Each on shot against the first off run's, by gate 1's rule against the
   off-against-off pair. A pop would be a patch in one shot; none allowed.
3. **GPU time, still views.** In one exclusive window per map, each view: off, on, off again (400 frames each); the on
   median at most the mean of the two off medians plus 0.15 ms, on every view.
4. **Kabuki's lap timing.** timedemo at 3840x2160 (the owner's settings), off and on interleaved, two runs each, in one
   exclusive window: the on mean frame rate at least the off mean less 1%, and its GPU frame median no worse than the
   off median plus 0.15 ms.
5. **Sanity.** No crash or Vulkan error in any run; phase 2 draws what phase 1 hid and is now visible
   (`r_sceneOcclusionStats`), and with the camera still it draws almost nothing.

If any gate fails, the cvar stays 0 and the failure is reported, not tuned around.

### The implementation (committed 2873dbc8, after round 2; not installed)

- **Phase 1** (`R_SceneVkBeginFrame`): the eye view's cull also tests each instance against the last frame's pyramid
  with the camera it was made with (`scene_cull.comp`, counts.z 1); one wholly behind it is not drawn but listed (an
  append list, and the size of phase 2's dispatch with it).
- **Phase 2** (`vk_scene_occlusion`, called by `RB_DrawSurfs` right after the eye's depth prepass): the main pass ends
  (as `RB_CharOcclusion` does), `scene_occ.comp` builds this frame's far-depth pyramid from the depth attachment itself
  (every MSAA sample, the farthest, so it can hide nothing visible; five levels a dispatch through shared memory), the
  cull runs again over the listed instances only (an indirect dispatch) with this frame's camera into a spare view slot
  (counts.z 2), recording each draw command it is first to touch; `scene_compact.comp`'s append mode adds just those
  commands to the slot's lists; and the pass resumes (`main_reload`) drawing their depth. Every later pass of the eye
  view (lit, blend, glow) draws the slot's lists after its own. The pyramid is the next frame's phase 1 pyramid too (two
  regions, alternating): nothing is built at the end of the frame and nothing depends on the reflections' capture.
- **Scope:** the eye view with GPU-driven draws and the depth prepass on (the defaults); sun cascades and other views
  unchanged. `r_sceneOcclusion` (1 since it passed), `r_sceneOcclusionStats` (SCENEOCC lines).
- **Its cost** (round 2, the same on both maps): the pyramid 0.13 ms, phase 2 0.01-0.02 ms, the pass split 0.09 ms:
  0.24 ms a frame. Round 1 cost 0.36 ms on Kabuki and 0.54 on the slice (phase 2 went over every instance and its
  lists were packed over every draw command by one workgroup; the pyramid took twelve dispatches).

### Round 1 gate results (2026-10-31, evidence in `occ/` with `_r1` names)

| Gate | Result |
|---|---|
| 1. Still views, images | **FAIL as written**: 12 of 18 views pass; 6 fail (`gate1_*.txt`, `viz/`). Every failing patch is the player's lit saber (its edge, its light on a wall), the saber's sparks, a traffic signal changing phase, or the wet street's reflection band: content that moves between the three shots, which are about 1,300 frames apart, so off-against-off does not cover the same pixels. A diagnostic with it held still (`--clean`: entities not drawn, scene dynamic lights and reflections off, signals held; not the gate): **18 of 18 pass**, on and off equal to 0.000-0.094% of pixels, the largest new patch 60 px (`clean2_*.txt`) |
| 2. Lap, moving camera, 90 shots | **PASS**, 90 of 90; no new patch in any shot (`lap/gate2.txt`). Along the lap phase 1 hid 31-62% of the indices (median 47%); phase 2 drew a median of 217 instances a frame (max 1,241, 1.6% of the indices): what would have popped is drawn in the same frame |
| 3. GPU time, still views | **FAIL, one view**: 17 of 18 pass. Slice: -0.12 to -6.92 ms (east to west 16.68 -> 9.76, street_d 14.84 -> 9.47, street_h 14.21 -> 10.09); Kabuki: -0.04 to -3.48 ms; **Kabuki street_i +0.18 ms** (6.84 -> 7.02; limit +0.15): little hidden there (4.1 M indices), the pass split costs more than it saves (`gate3_*.txt`) |
| 4. Kabuki's lap, 4K | **PASS**: 98.7 / 100.7 fps off, 102.6 / 105.3 on (+4.3%); GPU median 9.47 / 9.37 -> 9.14 / 9.08 ms (`gate4.txt`) |
| 5. Sanity | **PASS**: no crash or Vulkan error in any run; phase 2 draws while the camera moves and nothing while it is still |

**Decision, 2026-10-31: "Cut cost, re-gate"** (the pipeline session's, under the owner's delegation, not the owner's
own words; the choice offered: cut the split's cost, re-fix gate 1 as the
held-still comparison, rerun every gate). **Gate 1 from now on**, fixed before the rerun: the same views, shots and rule
(on against off within off-against-off: the share of pixels off by more than 16 at most 1.5 x plus 0.1 percentage
point; no new 8-connected patch of 400+ pixels off by more than 32), with the content that moves between shots held
still: entities not drawn (`r_drawentities 0`: the player, the saber, its sparks), the scene's dynamic lights and
reflections off, the signals held (`r_sceneSignalAt 5`) (`occ_gate.py --clean`). The as-played shots are still taken and
reported, not gated. Gates 2-5 unchanged.

**Round 1's notes for the owner** (before the choice above):

1. Gate 1's rule cannot pass while the saber, its sparks, the signals and the reflections move between shots: the
   evidence that occlusion hides nothing visible is gate 2 (90 of 90) and the held-still diagnostic (18 of 18).
   Re-fixing gate 1 as that held-still comparison would be a change of gate, so it is the owner's call.
2. Gate 3's failure is real: where little is hidden, the split's fixed cost (0.27 ms on Kabuki) wins. Proposed fixes,
   to gate again: phase 2 dispatched only over phase 1's flagged instances (an indirect dispatch over a compacted list
   instead of every instance), and the pyramid's first levels built in one pass; or keep the split out of frames where
   the last one hid little. None is implemented.

### Round 2 gate results (2026-10-31; one binary, sha256 in `occ/binary_r2.sha256`; all PASS)

The cost cut (above) then every gate again on that one binary; the timing window under `GAME_LOCK_EXCLUSIVE` with the
collision session's rebuild suspended (13:17-13:33). Then `r_sceneOcclusion` was made 1 by default (the only change; a
lap run checked it on without the cvar set: the same counts).

| Gate | Result |
|---|---|
| 1. Still views, held still | **PASS**: 10 of 10 slice views, 8 of 8 Kabuki (`gate1_*.txt`) |
| 2. Lap, moving camera | **PASS**: 90 of 90 shots, no new patch (`lap/gate2.txt`) |
| 3. GPU time, still views | **PASS**: 18 of 18 no worse (`gate3_*.txt`). Slice street views -0.52 to -5.08 ms (street_d 14.97 -> 9.89, street_h 14.53 -> 10.08, street_c 11.10 -> 9.62); its wall-facing views -8.0 to -9.4 ms; Kabuki -0.05 (the ring) to -3.63 ms; Kabuki street_i -0.14 ms (round 1: +0.18) |
| 4. Kabuki's lap, 4K | **PASS**: 103.9 / 104.9 fps off, 109.8 / 107.7 on (+4.2%); GPU median 8.94 / 9.02 -> 8.62 / 8.77 ms (`gate4.txt`) |
| 5. Sanity | **PASS**: no crash or Vulkan error; along the lap phase 2 draws a median 217 instances a frame (max 1,241), still views none (`../smoke3/lapstats`, `default_on`) |

The off windows of this timing run read higher than round 1's on some views (the slice's east to west 19.7 and 22.0 ms
against 16.6 and 16.8; Kabuki street_a 12.6 against 9.2): the machine's state, not the change (each gate compares on
against the off windows either side of it in the same load). As played (the saber, its sparks, the signals and
reflections moving), the round 1 report stands: no hole traced to occlusion.

## 1c. Tiled scenes: the loader (2026-10-31)

The pipeline session's next step under the owner's delegation ("you keep things running and progressing"): load a
tiled scene (docs/levelport-scene.md, "Tiled scenes, version 1": `maps/<map>.xman`, geometry shards, one material
table, tile files; every index global). Its shards end to end are byte for byte a single scene's sections, so the
loader joins the files listed by the manifest into one scene in memory, sized from the manifest's counts and each
file's header checked against them, and the rest of `R_SceneLoad` runs unchanged. A manifest is read before a
single `.xscn` (first gate run: the installed Install 50 pack's `cp_watson_slice.xscn`, scene-3, hid the staged tiled
scene-21 while `.xscn` came first; that run is kept as `tiled_gate_r1.log`).

**Gates, fixed before running:**

1. **Same scene.** The slice's tiled scene-21 (the pipeline session found it byte-identical to scene-20 in every
   section but the lights' order, which tile grouping changes) loads with the same `sceneinfo` counts as scene-20
   (instances, meshes, triangles, materials, draw commands, lights).
2. **Same image.** The occlusion gate's still views on the slice (its 10), held still as gate 1 above: scene-21 against
   scene-20 must sit in scene-20's own noise (scene-20 shot twice in separate loads), by gate 1's rule (the share of
   pixels off by more than 16 at most 1.5 x plus 0.1 percentage point; no new 400-pixel patch off by more than 32).
3. **Watson loads** (`cp_watson`, the 64-bit client, tiled scene-1 with the collision map the collision session
   names): the load completes without a Vulkan or loader error and a view renders; reported, not gated: load time,
   peak commit and working set, VRAM, the per-view visible buffers' size.

**Round 1 results** (loader build `%LOCALAPPDATA%/xenjk_build_p3_tiled64`, worktree `.codex-research/wt-p3-remap`;
evidence `occ/tiled*`, `occ/tdemo*`, `occ/watson/`):

| Gate | Result |
|---|---|
| 1. Same scene | **PASS**: scene-21's 27 files join into 521,448,704 bytes and load with scene-20's counts exactly (275,447 instances, 9,718 meshes, 11,683,217 triangles, 10,202 materials, 8,879 lights, 46,325 draw commands, 5,495 images) |
| 2. Same image, as written | **FAIL**, 3 of 10 views (`occ/tiled_gate.log`). The new patches trace every edge of the frame: the camera stood a sub-pixel apart. Each view drops the player 3 m onto the street, and where it lands follows the frame timing of that load; scene-20's two loads happened to land alike |
| 2, the camera locked (a diagnostic, not the gate) | The slice demo's fixed frames (timedemo replays the same snapshots), held still, scene-20 twice and scene-21 once: **6 of 6 frames at 0.000% of pixels off** (under 0.0005%, at most about 40 of 8.3 M pixels; not checked pixel for pixel, see 1g) in either pair (`occ/tdemo_gate.txt`) |
| 3. Watson loads | **PASS**: `cp_watson` with collision map col-2, the 64-bit client: 181 files joined into 1,311,321,592 bytes; 1,610,399 instances of 21,282 meshes, 28,396,087 triangles, 18,546 materials, 41,398 lights, 8,618 images (none missing), 112,797 draw commands; no Vulkan or loader error; four street views render (`occ/watson_views.jpg`) |

Reported for Watson: the scene's load 35.8 s of 55-72 s in all; peak commit 12.1 GB (8.4 GB of it the driver's
write-combined mirror of VRAM, section 3), working set 5.1-6.0 GB, VRAM 8.1-8.3 GB. The per-view visible buffers:
6.29 M slots, 24 MB a view, 384 MB for 8 views x 2 frames; sized by one LOD's parts they would be 2.72 M slots, 166 MB
(it needs a counter shared across a mesh's LODs: not done). The engine's zone total prints negative past 2 GB (an
`int`), a reporting quirk with the 2 GB collision map loaded.

**Round 2 gates** (the pipeline session, under the owner's delegation: "matching images (held-still method), the same
instance, mesh and light counts, and timing no worse"; fixed before running, on one binary, the loader build above):

1. **Counts**: scene-21 loads with scene-20's counts (as gate 1).
2. **Images, held still with the camera locked**: the slice demo's six fixed frames (timedemo replays the same
   snapshots), entities not drawn, the scene's dynamic lights and reflections off, the signals held; scene-20 twice and
   scene-21 once in separate loads; gate 1's rule of section 1b (on against off within off-against-off, no new
   400-pixel patch). The screenshot views of round 1 (the player dropped onto each street) stay reported, not gated.
3. **Timing no worse**: in one exclusive window, the slice's 10 views, GPU frame median with occlusion on (its default):
   scene-21 at most the mean of two scene-20 loads plus 0.15 ms, on every view.
4. **Watson, reported**: in the same window, load time, peak commit, working set and VRAM, and GPU frame medians on
   street views with occlusion on and off.

Gate 2 as amended is the pipeline session's decision (under the owner's delegation): "Re-fix gate 2 as the
camera-locked demo comparison, keeping the as-written result beside it in 1c. The failing patches come from the
landing spot following frame timing, so the gate compared two camera positions."

**Round 2 results** (one exclusive window, 14:46-15:05; the loader build, sha256 in `occ/loader_binary_r2.sha256`, a
copy in `occ/dll/loader/`; Watson with collision map col-4; logs `occ/loader_timing.log` for gate 2 and
`occ/loader_timing2.log` for the rest. The first run of the rest wrote its three slice loads into one folder, a quoting
slip in the script, and was stopped and rerun in the same hold: `occ/loader_killed_r2/`):

| Gate | Result |
|---|---|
| 1. Counts | **PASS**: scene-21 (27 files, 521,448,704 bytes joined) and both scene-20 loads print the same counts |
| 2. Images, camera locked | **PASS**: 6 of 6 frames, 0.000% of pixels off in either pair (`occ/tdemo_gate.txt`) |
| 3. Timing no worse | **PASS**: 10 of 10 views, scene-21 -0.02 to +0.04 ms from the scene-20 mean (`occ/loader_gate3.txt`) |
| 4. Watson | Reported below |

Watson on the loader build, held still, occlusion on (`occ/loader_watson.txt`, passes `occ/loader_watson_passes.txt`):
the load 50 s (scene joined and built in 23 s of it). GPU frames 14.9-17.0 ms (59-67 fps), p99 within 0.1 ms of the
median on every view; occlusion takes 2.3-22.7 ms off (from 15.0-38.9 ms), hiding 88-96% of the indices. The memory
probe's last reading came as the client exited and the probe stopped there (fixed). Round 1's col-2 figures stand
until it is rerun.

| View | Off, two loads | On | Largest passes, on |
|---|---|---|---|
| slice_street_c | 14.97, 19.59 | 15.01 | haze 4.28, lit 3.54, light clusters 2.17, depth 0.93 |
| slice_street_h | 38.48, 38.45 | 17.03 | haze 4.52, lit 3.75, clusters 2.22, cascades 1.97 |
| slice_street_d | 38.89, 38.96 | 16.25 | haze 4.54, lit 3.57, clusters 2.22, cascades 1.53 |
| kabuki_street_a | 29.21, 29.21 | 16.27 | haze 4.33, lit 3.77, clusters 2.16, cascades 1.46 |
| kabuki_ring | 24.98, 24.99 | 15.67 | haze 4.47, lit 4.06, clusters 2.18, cascades 1.19 |
| slice_elevated | 20.55, 20.55 | 14.86 | haze 4.68, lit 3.02, clusters 2.19, depth 0.88 |

On Watson the passes that scale with the light count lead: the haze pass (4.3-4.7 ms against the slice's 0.2-0.4)
and the light clusters (2.2 ms against 0.6), with 41,398 lights to the slice's 8,879. The cull is 0.65-0.73 ms. The
sun cascades are not among the largest here. These six views are the slice and Kabuki market streets; views across
Little China and Northside follow (section 1d's window).

The visible-buffer sizing was first left as it was (round 1's reading: one LOD's parts at most needs each draw
command's range known before the cull fills it, a count pass and a prefix sum); the pipeline session then asked for
it under the owner's delegation. Section 1d.

## 1d. Visible buffers: one range per LOD

**The change** (scene_cull.comp, vk_scene.cpp; worktree, on top of the loader). Each view's visible list held a range
for every part of every LOD, each with room for every instance of its mesh: 6.29 M slots on Watson, 24 MB a view, 384
MB for 8 views x 2 frames. An instance draws every part of the one LOD it picks, so a LOD's parts draw the same
instances: they now share one range of the LOD, with room for the instances of the meshes using it. The cull counts the
instance on the LOD's first part's command and raises the other parts' counts to the same value (an atomic max); the
draw commands keep their own index ranges and lists, and the vertex shader is unchanged. One write to the list a
visible instance instead of one a part. Sizing by one LOD's parts at most (2.72 M slots, 166 MB, the figure in round
1) is not reachable with ranges fixed at load: two LODs of a mesh both drawing part slot k cannot share it without
knowing their counts first, which is the count pass and prefix sum above. Watson goes to 4.18 M slots, 16 MB a view,
255 MB (`visible_size.py`). A part listed by two LODs would break the sharing: no scene does (`partshare.py`: Kabuki,
the slice, both tiled scenes), and a scene that did is now refused at load like one with references out of range.

**Gates, fixed before running** (the pipeline session's: images and counts unchanged, timing no worse):

1. **Counts**: scene-21 loads with the same `sceneinfo` counts on the loader build and on this one.
2. **Images**: the camera-locked held-still frames of section 1c's round 2 (the slice demo's six), this build against
   the loader build shot twice, by gate 1's rule of section 1b.
3. **Timing no worse**: in one exclusive window, the slice's 10 views with occlusion on, scene-21: GPU frame median at
   most the mean of two loader-build loads plus 0.15 ms, on every view.
4. **Memory, reported**: Watson's VRAM against the loader build's.

**Results** (one exclusive window, 15:11-15:35; the LOD build sha256 in `occ/lod_binary.sha256`, a copy in
`occ/dll/lod/`, its source `occ/lod_ranges_worktree.patch`; the loader build `occ/dll/loader/`; log `occ/lod_gate.log`.
The pipeline session's offline per-tile drop check, Python on the CPU with no game client, ran through this window and
the round-2 loader window; the A/B/A comparisons share it, the absolute Watson figures carry it. Processes at the start
of the timing: `occ/lod_window_procs_1514.txt`):

| Gate | Result |
|---|---|
| 1. Counts | **PASS**: the same `sceneinfo` line in all six loads |
| 2. Images | **PASS**: 6 of 6 frames at 0.000% off against both loader-build shots (`occ/lod_gate2.txt`; gate 1's rule, not pixel for pixel: see 1g) |
| 3. Timing no worse | **PASS**: 10 of 10 views, -0.03 to +0.06 ms from the loader mean (`occ/lod_gate3.txt`; its columns say scene-20 and scene-21, here the loader and LOD builds, both on scene-21) |
| 4. Memory | Watson's VRAM 8,214 MB against 8,322 MB (-108 MB, against 129 MB computed); peak commit 12.0 GB either way (`lod/watson_mem_{l,n}/probe.txt`) |

The cull pass itself is about 0.01 ms slower on the slice (0.148-0.175 ms against 0.151-0.164; every visible instance
now counts on one command and raises the others'), inside the frame's noise.

## 1e. Watson in the 64-bit client: the district's street views (2026-10-31)

The pipeline session's step 2: Watson's frame times at 4K in the 64-bit client, occlusion on, real street views across
the district, a fixed-camera lap, median, p95 and p99 per view, and which passes dominate. Same window as 1d, the
committed loader build (fca0b281's rd-vulkan), collision map col-4 (xenjk7), entities drawn (the owner's
openjk.cfg, third person).

**Views** (`find_sightlines2.py`: the longest street-level sightlines starting inside each subdistrict's outline from
the game's location areas; `make_lap.py` lists them; screenshots `occ/watson_district_views.jpg`): three in Little
China, two in Kabuki (the slice and market views of 1c are Kabuki and its Little China edge), three in Northside, and
one picked as an elevated highway that landed inside a covered structure instead (reported, not a street).

| View | GPU median / p95 / p99 ms, on | fps | Off, two windows | Hidden | Largest passes, on |
|---|---|---|---|---|---|
| lc_street_a | 13.59 / 13.62 / 13.63 | 74 | 13.60, 31.57 | 97.4% | haze 4.37, clusters 2.14, lit 1.70, cascades 1.21 |
| lc_street_b | 14.99 / 15.09 / 15.11 | 67 | 18.46, 18.44 | 87.8% | haze 4.57, lit 3.31, clusters 2.14, cascades 0.92 |
| lc_street_c | 15.49 / 15.52 / 15.53 | 65 | 37.10, 37.38 | 84.1% | haze 4.34, lit 3.16, clusters 2.12, depth 1.98 |
| kabuki_street_b | 16.69 / 16.73 / 16.74 | 60 | 25.91, 25.91 | 91.0% | haze 4.57, lit 3.71, clusters 2.18, cascades 1.83 |
| kabuki_street_c | 15.30 / 15.39 / 17.56 | 65 | 23.76, 23.73 | 96.1% | haze 4.66, lit 3.34, clusters 2.18, cascades 0.92 |
| ns_street_a | 11.30 / 11.36 / 11.41 | 88 | 11.16, 11.17 | 34.6% | haze 4.23, clusters 2.02, lit 1.15, main after scene 0.81 |
| ns_street_b | 15.09 / 15.18 / 15.21 | 66 | 26.27, 26.27 | 87.6% | haze 4.49, lit 3.27, clusters 2.16, depth 1.18 |
| ns_street_c | 12.37 / 12.42 / 12.46 | 81 | 20.89, 20.88 | 94.5% | haze 4.37, clusters 2.10, lit 1.67, main after scene 0.72 |
| (highway_a, covered) | 11.61 / 11.63 / 11.64 | 86 | 15.28, 15.26 | 96.5% | haze 4.45, clusters 2.10, cascades 1.03 |

(`occ/watson_district_gate.txt`, `occ/watson_district_passes.txt`; held still, so p95 and p99 sit on the median.)
Watson's streets run at **11.3-16.7 ms, 60-88 fps**: none reaches 120 (8.3 ms). Two passes take 6.2-6.8 ms of every
view whatever it shows, even the open Northside road with 3.5 M indices: the **haze** (4.2-4.7 ms) and the **light
clusters** (2.0-2.2 ms). On the slice (8,879 lights) they are 0.2-0.4 and 0.6 ms; Watson has 41,398. The sun
cascades (`scene shadow`, 0.3-1.8 ms) come third or fourth at most. Without those two passes every street view would
be 4.5-10.5 ms. The cull is 0.59-0.65 ms.

**The lap** (`make_lap.py`, `watson_lap.dm_26`, `occ/watson_lap.txt`): three street runs (Little China 300 m,
Kabuki 250 m, Northside 250 m) recorded with the camera stepped 1 m a command, which the demo keeps as about 5 m a
frame (169 frames). Replayed twice by timedemo, the demo's 169 frames: GPU 17.6-17.8 / 33.4-34.5 / 34.7-36.6 ms
(median / p95 / p99). The 33 ms frames are of two kinds: the first frames after the load, every pass doubled (the
GPU clocking up), and frames where the depth prepass takes 8-9 ms instead of about 1: at 5 m a frame, last frame's
depth pyramid no longer covers this
frame's view, and occlusion's first phase hides far less. That stride is about seventy times a running player's at
120 fps, so this lap overstates it.

**The walking-pace lap** (`make_lap.py 0.02 20 watson_walk`: the same three streets, 20 m each, the camera stepped 2
cm a command, about 0.1 m a demo frame, 667 frames; the commands chained through `zz_watson_walk_cfg.pk3`, as one
6,000-line config overflows the engine's command buffer; logs `occ/walk_timing*.log`). Replayed twice in exclusive
windows:

| Replay | GPU median / p95 / p99 ms | Largest passes |
|---|---|---|
| Entities drawn (`occ/watson_walk.txt`) | 19.9-20.8 / 23.4-23.7 / 28.4-30.3 | main after scene 4.4-4.9, haze 4.4, glow extract 2.8-3.1, clusters 2.2 |
| Entities not drawn, first person (`occ/watson_walk0.txt`) | **13.2-13.5 / 16.0-16.5 / 25.5-25.6** | haze 4.4, clusters 2.2, lit 1.8-2.0, cascades 1.3, depth 0.9 |

With entities drawn, the third-person camera hugs the player in the walled Little China road and the saber's blade
fills much of the screen (`occ/watson_walk_shots.jpg`): `main after scene` and the glow passes then measure the blade,
not the city. That replay is kept but not read as the district's cost. Its middle shot also shows the road black
around the player, which no held-still view shows; not investigated. Scene only, the three streets run at 13.5
(Little China), 16.2 (Kabuki) and 10.7 ms (Northside) medians, the held-still views' figures; the p99 is the first
frames after the load. At walking pace the depth prepass stays at about 0.9 ms: occlusion holds in motion.

## 1f. The haze pass, profiled (2026-10-31)

The owner's step after 1e: where Watson's 4.2-4.7 ms haze goes. Measured before changing anything.

**What the pass does** (`vk_scene_haze`, `scene_fog.comp`). A light volume over the eye's frustum, 240 x 135 froxels
across by 64 exponential slices (2.07 M froxels). The inject dispatch lights each froxel's centre: the haze's
density, plus the local haze volumes' (the source's fog volumes and steam columns, `maps/<map>.xfog`), its light
cluster's lights with their shadow maps, and the moon through the sun cascades. A second dispatch integrates each
column toward the eye. Before testing volumes, each 8 x 8 group of froxels builds a mask of the volumes that can
reach it, one bit each in shared memory: `FOG_MASK_WORDS = 8`, so **up to 256 volumes**. Past that, every froxel
tests every volume. Watson has **606** (`scene haze volumes maps/cp_watson.xfog: 606`), the slice 154.

**How it was measured** (`haze_prof.py`, `haze_parse.py`, `haze_timing.sh`; one exclusive window, 19:34-19:38, after
the collision session suspended its compiles; the committed renderer a32d66cf). Four Watson street views from 1e and
two slice views, held still, 300-frame `r_gpuTimes` windows: the defaults, then one cvar changed at a time, then the
defaults again (drift under 0.1 ms):

| Window | Cvar | Watson: haze pass change | Watson: frame change | Slice: haze / frame change |
|---|---|---|---|---|
| Volumes off | `r_sceneFogVolumes 0` | **-4.11 to -4.31 ms** (to 0.11-0.39) | -4.12 to -4.32 | -0.05 / -0.02 to -0.09 |
| Moon off | `r_sceneFogMoon 0` | -0.53 to -0.76 | -0.53 to -0.71 | -0.02 to -0.03 / about 0 |
| Light shadows off | `r_sceneLightShadows 0` | 0.00 to -0.04 | -0.05 to -0.13 | about 0 |
| Clusters off (grid cells) | `r_sceneClusters 0` | 0 to +0.13; cluster pass -1.18 to -1.36; lit +0.01 to +0.91 | -0.27 to -1.33 | cluster pass -0.17 to -0.22; lit up to +0.62 |
| Haze light off | `r_sceneFogLight 0` | -4.22 to -4.54 (the pass); cluster pass -0.71 to -0.76 | -5.06 to -5.31 | -0.32 to -0.38 / -0.43 to -0.45 |

(`occ/haze_watson.txt`, `occ/haze_slice.txt`.)

**Reading.**

- **The volumes are the haze's cost on Watson: 4.1-4.3 of its 4.2-4.5 ms**, on every view whatever it shows. With
  606 volumes past the mask's 256, each of the 2.07 M froxels runs the box test for all 606 (about 1.25 G tests a
  frame). The slice's 154 stay under the cap and cost 0.05 ms. Without the volumes Watson's haze is 0.1-0.4 ms, the
  slice's level.
- The moon's 0.5-0.7 ms is measured with the volumes on. It is the cascade lookups competing with the volume loop for
  the same threads; with the volumes cheap it should shrink toward the slice's 0.03 (to re-measure).
- The light clusters: building them is 1.2-1.4 ms of Watson's 2.0-2.2 ms "pack, clusters" pass (the rest is the
  draw lists' packing), 0.7 ms of it the haze's half. Every one of the 960 tiles (twice with the haze) tests every
  light in the scene, 41,398 on Watson: the cost follows the district's light count, not the view. Turning them off
  costs the lit pass up to 0.9 ms, so they stay.

**Recommended first change: lift the volume mask's cap** (`scene_fog.comp`, `FOG_MASK_WORDS` 8 to 32: 1,024
volumes, 128 bytes of shared memory a group). Each group then tests the 606 volumes once, about 10 per thread, and each
froxel only the few that reach its group. The shader's comment already guarantees the same volumes in the same order,
so the images should be bit-identical: gate on identical held-still frames, and timing no worse on the slice. Expected
saving about 4 ms on every Watson view: its streets from 11.3-16.7 ms to about 7.2-12.5 ms, Northside's open road
about 140 fps. A district of Night City with more than 1,024 volumes would need the mask to grow, or the volumes
binned per froxel tile once a frame.

**Second:** cull the lights to the eye's frustum and the clusters' far distance once a frame before the tile tests,
so each tile walks the lights in view, not the district's. Not measured; the tile tests' 1.2-1.4 ms is the ceiling.

## 1g. The haze volume mask: 1,024 volumes (2026-10-31)

The pipeline session's decision under the owner's delegation: build and gate 1f's first change. **The change**
(`scene_fog.comp` only): `FOG_MASK_WORDS` 8 to 32, so each group's mask holds up to 1,024 volumes, and each froxel
walks only the mask words the scene's volumes fill (5 on the slice, 19 on Watson) instead of all of them. Worktree
`.codex-research/wt-p3-remap` on a32d66cf; build `occ/dll/haze1/` (sha256 `occ/haze1_binary.sha256`), its source
`occ/haze1_worktree.patch`.

**Resources** (`r_sceneShaderStats 1`, the driver's statistics; `occ/haze_stats_{lod,haze1}.txt`): the inject
shader keeps 55 registers and no stack; its shared memory grows from 32 to 128 bytes a 64-thread group. Registers bound
its occupancy and are unchanged; 128 bytes a group is far under any limit a register-bound group count reaches.
**Occupancy does not drop.**

**Gates, fixed before running** (one exclusive window; a32d66cf's build "lod" against "haze1"):

1. **Frames bit-identical**: camera-locked demos (`make_hold.py`: the camera held with noclip at four Watson street
   views and two slice views, recorded, replayed by timedemo), held still (entities not drawn, first person, the
   scene's dynamic lights and reflections off, the signals held); a screenshot every 12th frame over Watson's 312
   frames and every 10th over the slice's 80; lod twice, haze1 once. PASS when every haze1 frame matches lod's
   exactly (0 pixels off) and lod matches itself.
2. **Slice timing no worse**: the slice's 10 views, lod / haze1 / lod: haze1's GPU median at most the lod mean plus
   0.15 ms on every view.
3. **Watson's haze pass, reported**: 1f's ablation windows on the four Watson views, haze1 and lod in the same window;
   the moon re-measured with the fix in.

**Results** (exclusive windows 20:04-20:25 and 20:26-20:34, the collision and pipeline sessions' work stopped; logs
`occ/haze_gate.log`, `occ/haze_gate2.log`):

| Gate | Result |
|---|---|
| 1. Frames bit-identical | **FAIL as written** (`occ/haze_gate1.txt`): the baseline is not bit-identical to itself. lod's two runs differ on 22 of 32 frames by 0-322 pixels, and haze1 by the same amounts |
| 1, a diagnostic (`occ/exact_why.py`, `occ/haze_gate1_why.txt`) | The pixels where haze1 differs from both lod runs while those agree: 0-183 a frame, at most 28 levels, all in the same few small spots where lod flickers between runs (about x 1067 y 855 and x 270-280 on Watson, the row y 1000 on the slice). No broad change anywhere |
| 2. Slice timing no worse | **PASS**: 10 of 10 views, -0.03 to +0.08 ms (`occ/haze_gate2.txt`; the first attempt's three loads overwrote one folder, a quoting slip, and were rerun in their own window: `occ/hazeg$r`) |
| 3. Watson's haze pass | Below |

| View | lod: frame / haze ms | haze1: frame / haze ms | haze1: volumes off / moon off (haze change) |
|---|---|---|---|
| lc_street_a | 13.70 / 4.42 | **9.64 / 0.39** | -0.18 / -0.02 |
| kabuki_street_b | 16.69 / 4.58 | **12.58 / 0.60** | -0.22 / -0.06 |
| ns_street_a | 11.39 / 4.25 | **7.33 / 0.32** | -0.21 / -0.03 |
| ns_street_b | 15.19 / 4.52 | **11.05 / 0.39** | -0.18 / -0.03 |

(`occ/haze_gate3_h.txt`, `occ/haze_gate3_l.txt`, same window.) The fix takes 4.0-4.1 ms off every Watson view;
Northside's open road runs at 7.3 ms (137 fps). The volumes now cost 0.18-0.22 ms. **The moon was volume-bound**:
0.53-0.76 ms before, 0.02-0.06 now.

Gate 1 failing as written was left to the pipeline session's decision; the fix waited uncommitted.

**The pipeline session's decision** (under the owner's delegation): no amending gate 1 after a run failed it. The
baseline's flicker is a finding in its own right, flicker a player can see; find its cause and fix it if it is a
defect, then rerun gate 1 unchanged. Volume fix uncommitted until then; gates 2 and 3 stand.

**The flicker's cause** (`flicker_diag.sh`, `occ/flicker_serial.txt`, `occ/flicker_normal.txt`): every pixel that
flickers is on a one-pixel seam or an isolated pixel where two instances draw the same depth. The depth prepass keeps
one of them, the lit pass's EQUAL test passes both, and the last drawn wins. A command's instances are drawn in the
order the cull's atomics handed out their slots, which changes run to run. A diagnostic build (`occ/dll/flickerdiag`,
never committed) that runs every cull as one workgroup (so the atomics land in the same order) cut the frames that
differ between two runs from 17 of 32 to 8 of 32; those 8 are the first frames after the load and the frames after the
camera moves, where occlusion's phase 2 draws: its appends of commands to its lists were atomic too.

**The fix: order-stable visible lists** (worktree `.codex-research/wt-p3-order` on a32d66cf; build `occ/dll/order1/`,
source `occ/order1_worktree.patch`). The pipeline session named the standard fix: mark, scan, write each visible
instance at its scan index. Here the draw ranges are per LOD (1d), so one scan over all instances would still need a
stable partition by LOD; the same result, each range in instance order whatever the timing, is had more cheaply by
sorting each range after the cull. `scene_cull.comp` lists every LOD whose range gets a second instance, for the views
the lit pass draws (the eye and phase 2's slot; the sun cascades and light shadows draw depth alone, where order
cannot change the result); `scene_sort.comp` sorts each listed range by instance index, one workgroup a range (a
bitonic sort in shared memory up to 2,048 entries, in place beyond). Phase 2's slot is packed like any view, in command
order, instead of its touched commands being appended through atomics.

**Gates, fixed before running** (one exclusive window):

1. **The baseline reproduces itself bit for bit**: make_hold.py's camera-locked frames (as gate 1 above), the order
   build twice in fresh runs: 0 pixels differ on every frame.
2. **Gate 1 on the volume fix, unchanged**: the volume fix on the order build (`occ/dll/orderhaze1/`) once against
   those two runs: every frame matches both exactly.
3. **The cull's cost, reported** against today's atomic path (a32d66cf's build, lod): the slice's 10 views and the
   four Watson views, lod / order / lod in the window, GPU frame medians and the sort pass's own time.

**Round 1 results** (exclusive window 20:57-21:20; `occ/order_gate.log`):

| Gate | Result |
|---|---|
| 1. The baseline reproduces itself | **PASS**: 0 of 32 frames differ, 0 pixels (`occ/order_gate1.txt`) |
| 2. Gate 1 on the volume fix | **PASS**: 0 of 32 frames differ from either run (`occ/order_gate2.txt`) |
| 3. Cost | **Too much**: slice +0.28 to +0.41 ms on all 10 views, Watson +0.73 to +0.86 ms on all 4 (`occ/order_gate3_{slice,watson}.txt`) |

The sort itself is small (0.007-0.013 ms on the slice, 0.02-0.05 on Watson). The cost is phase 2: packing its slot
like a whole view is one workgroup walking every draw command (46,325 on the slice, 112,797 on Watson), and the phase
2 pass went from 0.02-0.03 ms to 0.29-0.30 on the slice and from 0.05-0.08 to 0.72-0.75 on Watson. That is reported
before choosing, as the pipeline session asked, and changed below; the order build is not committed.

**Round 2: phase 2 appended in sorted order.** Phase 2 lists the commands it touches again, as before; the touched
list is sorted (`scene_sort.comp` with `kTouched`, one workgroup; usually a handful of commands, none when the camera is
still), and `scene_compact.comp`'s append mode now takes the list in that order in one workgroup, each list's slots by a
scan over it, not by atomics. Builds `occ/dll/order2/` and, with the volume fix, `occ/dll/orderhaze2/`; sources
`occ/order2_worktree.patch`, `occ/orderhaze2_worktree.patch`. Gates as round 1 (1, 2, 3 above), fresh runs on these
builds (`order_gate2.sh`), round 1's results not carried over.

**A pre-check before round 2** (outside a timing window, while the collision session's game-client runs had resumed;
`order_repro.sh`, `frame12_states.sh`): order2 reproduced itself on 31 of 32 frames in two pairs of runs. Both times
Watson's first shot after the load (frame 12) differed by the same 18 scattered pixels, each one level, between the
pair's first and second run; the two variant frames match each other exactly, and the other runs match order1. Over
ten runs order2 showed the variant twice (both in the minutes after 21:20, beside the collision session's clients),
order1 never in ten. Two fixed outcomes rather than noise suggests something in the first frames after a load that
follows timing (which of two states a load lands in) rather than the draw order; not resolved, and it reads 2 of 10
against 0 of 10, so it is reported, not explained away.

**Round 2 results** (exclusive window 21:47-22:10, `occ/order_gate2.log`; the collision session's last run stopped at
21:48, so the first Watson image run overlapped it for its first half minute):

| Gate | Result |
|---|---|
| 1. The baseline reproduces itself | **PASS**: 0 of 32 frames differ, 0 pixels (`occ/order2_gate1.txt`) |
| 2. Gate 1 on the volume fix, unchanged | **PASS**: 0 of 32 frames differ from either run (`occ/order2_gate2.txt`) |
| 3. Cost against the atomic path | Slice -0.18 to +0.11 ms on the 10 views, Watson +0.02 to +0.11 ms on the 4 (`occ/order2_gate3_{slice,watson}.txt`) |

The sort pass is 0.015 ms on the slice and 0.06 ms on Watson; phase 2 is back to its cost before (0.029 against 0.026
ms on the slice, 0.057 against 0.053 on Watson); the cull is unchanged.

**Committed** (the pipeline session's decision under the owner's delegation: "0.06 ms for frames that reproduce bit for
bit is worth it"): the order fix as cf415962, then the volume mask on it, renderer files and this report; not pushed,
not installed. **Open item: frame 12's two-state variant** (the pre-check above). Next, after the view cull: frame 12
alone in an exclusive window, 20 runs each of order1 and order2, to tell a real difference from contention; it may be
load-time residency (textures or LODs not settled), not draw order. Method, fixed before the run (`frame12_excl.sh`):
after a quiet minute, the two builds alternate, each run's frames 12 and 24 compared pixel for pixel with order1's
frame (state B) and with the variant (state A, rep_order2's first run); reported per build as counts of A, B and any
other outcome. Earlier
"identical" readings in 1c and 1d were gate 1's percentage rule at 0.000% (up to about 40 pixels), not pixel for pixel;
their wording is corrected above.

## 1h. Lights and haze volumes culled to the view (2026-10-31)

The pipeline session's step after the volume mask: cull the lights, and the haze volumes, to the view once a frame
before the clusters are built; that also lifts the 1,024-volume ceiling for the city. **The change** (worktree
`.codex-research/wt-p3-order` on e6837414; build `occ/dll/viewcull2/`, source `occ/viewcull2_worktree.patch`):
`scene_viewcull.comp`, one workgroup of 1,024 before the clusters, keeps each light whose sphere, and each haze volume
whose bounding sphere (computed at load from its box), can reach the eye's frustum: its four sides and the eye's plane,
with a margin of a unit and a ten-thousandth of the distance. The kept indices are written in ascending order (each
subgroup a contiguous run, the runs in order). `scene_cluster.comp` walks that list instead of every light; each
subgroup still takes the lights of the same index range as before (found in the list by binary search), so every tile
keeps the same lights in the same order and fills the same way. `scene_fog.comp`'s volume mask holds positions in the
volume list, in order, so each froxel sums the same volumes in the same order. `r_sceneViewCull 0` passes every index
(the lists stay whole), for comparison in one load.

**Gates, fixed before running** (one exclusive window; e6837414's build `occ/dll/orderhaze2/` the baseline):

1. **Frames bit-identical**: make_hold.py's camera-locked frames, the baseline twice against the view cull once: every
   frame matches both exactly (0 pixels), the baseline matching itself.
2. **Slice timing no worse**: the slice's 10 views, baseline / view cull / baseline: the view cull's GPU median at most
   the baseline mean plus 0.15 ms on every view.
3. **Watson, reported**: the four street views on the view cull with `r_sceneViewCull 0` and `r_sceneClusters 0`
   windows in the same load; the light clusters' pass against the baseline's.
4. **Resources, reported**: the shaders' registers and shared memory (`r_sceneShaderStats 1`) against the baseline's.

**Round 1 results** (window 22:15-22:36, `occ/vc_gate.log`; other GPU use during it: Chrome and the Settings app were
open, and the baseline's two slice loads disagree by up to 1.9 ms where earlier windows agreed within hundredths, so
its absolute timings are noisy):

| Gate | Result |
|---|---|
| 1. Frames bit-identical | **PASS**: 0 of 32 frames differ from either baseline run, 0 pixels (`occ/vc_gate1.txt`) |
| 2. Slice timing no worse | **FAIL**: 8 of 10 views (`occ/vc_gate2.txt`); noisy, but the cull's own cost is visible inside each frame: the packing-and-clusters section 0.57 to 0.90 ms |
| 3. Watson | In one load: the cluster pass 0.85 ms with the cull on the open Northside road (2.2 normally), but 2.3-3.3 ms on the three busier views and 3.8-4.2 ms with `r_sceneViewCull 0` (`occ/vc_gate3.txt`) |
| 4. Resources | The haze shader 55 to 58 registers (likely 36 to 32 warps a multiprocessor at NVIDIA's allocation steps of 8); the cull 44 registers, 516 bytes of shared memory (`occ/vc_stats_*.txt`) |

The culling works (the open road's clusters drop by 1.3 ms), but the pass that does it is too slow: one workgroup of
1,024 walked the 41,398 lights in steps of 1,024 with three barriers a step, on one multiprocessor, over a millisecond
on Watson; and its barrier between the packing and the clusters took away their overlap. Not committed.

**Round 2: the cull in two parallel passes** (`occ/dll/viewcull3/`, source `occ/viewcull3_worktree.patch`). Workgroups
of 256 over the lights and the volumes: the first pass counts each workgroup's kept ones; the second writes each kept
index at its workgroup's offset (the counts before it, summed by its threads) plus its rank in the workgroup, the
same ascending order. It runs at the start of the frame's culls in its own timed section ("scene view cull"), before
the packing, so the clusters run beside the packing again. Gates as round 1, fresh runs, on this build.

**Round 2 results so far** (window from 23:11, stopped at 23:30; `occ/combined_window.log`). Gate 1, frames
bit-identical: **PASS**, 0 of 32 frames differ from either baseline run (`occ/vc3_gate1.txt`). The timing gates were
not scored: the owner was using the PC (a video player, Steam and Chrome on the GPU), the baseline's two slice loads
disagreed by up to 1 ms, and the pipeline session ruled that noise, not a result: no whole-frame timing gate until the
PC is idle, and the slice gate neither scored nor amended. Taken in that shared window, as evidence only, the passes'
own time inside each slice frame: the view cull 0.005 ms, the packing and clusters 0.567 to 0.515 ms (`vcg3/n` against
`vcg3/sa`, `vcg3/sb`). Watson's runs and frame 12's check had not started. The view cull stays uncommitted until its
timing gates pass in an idle window.

**The idle window's rule, fixed before it** (the pipeline session: "if the baseline's two loads still disagree by more
than your noise allowance, report that and stop. Don't score"): gate 2 is scored only if the baseline's two loads agree
within 0.2 ms on every slice view (the clean windows today agreed within 0.15); otherwise it is reported unscored.
Fresh runs of all of round 2 (`combined_window.sh`: `vc_gate3.sh`, then `frame12_excl.sh`).

**The "idle" window** (23:31-23:43, `occ/combined_window.log`): gate 1 **PASS** again, 0 of 32 frames differ
(`occ/vc3_gate1.txt`). Gate 2 **not scored**: the baseline's two loads disagreed by more than 0.2 ms on 4 of the 10
slice views (0.25, 0.37, 0.24 and 0.29 ms; `occ/vc3_gate2.txt`), and its frames ran about 0.8 ms above the clean
windows' (east to west 9.3 against 8.5). With nothing of ours running, the GPU stayed at 28% utilisation: the video
player, Chrome and Steam were still open on it. Stopped there by the rule above; Watson's runs and frame 12's check not
started.

**The owner's decision** (relayed by the pipeline session): "bench with those running". The 0.2 ms clean-baseline
precondition is waived by the owner for this window; instead the method is made robust to the shared GPU (the pipeline
session's terms), **fixed before the run** (`ab_window.sh`, `bench_ab.py`, `ab_parse.py`):

- Baseline (A, e6837414's build) and view cull (B) loads interleaved, A B A B, four of each, on the slice's 10 views
  and then Watson's 4; one occlusion-on window of 300 frames per view per load.
- Scored per view on the paired differences B - A (each B against the A loaded just before it): their median and
  spread, beside the baseline's own noise floor in the window (the range of its four A medians).
- Gate 2 as in 1h, judged on the paired medians: PASS when the median of B - A is at most 0.15 ms, or inside the noise
  floor (then "no worse", not "faster"). Watson reported the same way.
- The per-pass GPU timestamps stay the primary evidence for where the time goes. Then frame 12's 20+20 check (an image
  check).

That run was stopped at its first load: the owner, relayed by the pipeline session, "an hour is way too long, youll
know fps in like 2 minutes". **The method again, fixed before the run** (`alt_window.sh`, `alt_bench.py`): each build
loads once, side by side in its own client; each loops over the held views (40 settle frames, then a 150-frame
`r_gpuTimes` window per view); the inactive client's process is suspended, so they take turns on the GPU, A B A B,
one full pass of the views per turn, three turns each. Per view the B - A differences paired by turn, their median
and spread, beside the spread of A's own three turns (the noise floor); the rule as above (PASS when the median is at
most 0.15 ms or inside A's spread). Slice, then Watson; frame 12's check cut to 5+5 runs (`frame12_quick.sh`).

**Results** (23:55-00:02, `occ/alt_window.log`, `occ/ab_watson_slice.txt`; the owner's video player, Chrome and Steam
open, by the owner's decision). **Slice: FAIL**, 3 of 10 views: south to north +0.31 ms (paired median; -0.02 to
+0.32), west to east +0.37 (+0.32 to +0.39), street d +0.20 (+0.19 to +0.38), each beyond A's own spread (0.16-0.18);
the other 7 no worse (-0.20 to +0.13). The two loads took 43 s and each turn (a pass of the 10 views) 9 s: the slice's
comparison took 98 s. In the passes: the view cull 0.005 ms, the haze 0.204 against 0.210 (the volume list costs
nothing measurable), the packing and clusters 0.545 against 0.560 over all views, but +0.16 to +0.21 ms on the three
failing views (`alt/slice`). There most of the slice's lights are in view, so culling removes few, and the runs each
subgroup takes (kept at their index ranges so every tile fills as before) are left uneven: the slowest subgroup sets
the time. **Watson: not scored.** Two Watson clients need about 2 x 8.3 GB against the card's memory; their load took
241 s (paging), so turns would measure paging, not the view cull (the pipeline session's ruling). Stopped there; the
owner then took the PC to play. Next for Watson: one client at a time, A, B, then A again. The view cull stays
uncommitted; its volume half (no measurable cost, and it lifts the 1,024-volume ceiling) could go on its own, its
light half needs the runs balanced.

**The pipeline session's ruling: split it.** (1) The volume half as its own change, gated on its own in minutes once
the owner is done playing: frames bit-identical on the 32, slice timing A/B/A with two clients, Watson one client at a
time; committed if it passes, renderer files only. (2) The light half reworked later, uncommitted; an idea to test,
not adopt: compact the visible lights in index order and split that list evenly across the subgroups, then check that
every cluster's list comes out as today before timing anything. **The volume half, coded** (not built while the owner
plays; worktree `.codex-research/wt-p3-order`, `occ/volcull1_worktree.patch`): `scene_viewcull.comp` culls the haze
volumes alone (the same two passes, one row of workgroups); the light list, the cluster shader's change and the
clusters' fourth binding are gone; the haze reads the volume list as before. The light half's last state is kept in
`occ/viewcull3_worktree.patch` and `occ/viewcull3_scene_viewcull.comp`.

## 1i. The 2026-11-01 review's defects: verified, fixed and gated

The review of `look/review-20261101/` (two Fable and three Codex reviews, while the owner played). Each finding below
was checked against the code before it was fixed; each fix is its own commit on a private branch
(`wip/p3-fixes` in `.codex-research/wt-p3-fix`, on e6837414), to land on the renderer branch one at a time once its
check passes (no runs until the owner is done):

| Finding | Verified | Fix |
|---|---|---|
| A lamp's shadow face never sets `rain`, and `Scene_RenderLightShadows` takes the rain view's path on it (Codex D1) | yes: `sceneShadowView_t view;` uninitialised, `view->rain` read at two sites | `view.rain = qfalse` (301cc736) |
| Normals turned by M, not inverse-transpose(M); a mirror keeps the tangent's handedness (Codex D6) | yes: `scene.vert` `inNrm.xyz * rows` | a rotation with a uniform scale keeps today's arithmetic (bit-identical there); others use M's cofactors with det's sign, a mirror flips the tangent's w; the load counts both kinds of placement (1753ce22) |
| Capacity cliffs pass silently (Fable D3) | yes | load warnings for the light grid's capped cells, lights past 65,535 (the clusters off), haze volumes past 1,024 (a420ae52); `r_sceneClusterStats` counts the tiles and clusters that fall back to the grid each frame, SCENECLUSTERS (6b65f203) |
| `tileFull` written by several subgroups at once (Codex D7) | yes (all write `true`; still a race) | one count per subgroup, reduced after the barrier: the same decisions (13cfe819) |
| The occlusion test has no margin (Fable D2) | yes: `zNear > d` | `zNear > d * (1 + 2e-5) + 1` (e086444c) |
| Async SSR and the grade used after a partial setup (Codex D2, D3) | yes: `asyncPool`, `gradePipe`, `gradeCompPipe` exist before their later allocations | readiness flags set only once everything is made (3b5b3144) |
| Validation spans wrap in 32 bits; no vertex-range check (Codex D4) | yes | 64-bit spans; each part's indices with its vertex offset inside the vertices (7a37cb7f) |
| A malformed manifest line copies an uninitialised path; a later `xman 1` clears the error (Codex D5) | yes | errors sticky, the parse stops at once (233dc0e8) |
| `r_sceneInvocations` misses phase 2's lists (Codex D8) | yes: the query sits inside the eye's list call only | the query wraps both list calls (642fb1b4) |
| The volume half assumes linear, full subgroups (Fable D7) | yes | ranks from a shared-memory scan over the local index; no subgroup operations (`occ/volcull2_worktree.patch`) |
| Coplanar ties follow history: phase 2's lists draw after the eye's (Fable D1) | the order, yes; whether it shows: to check | not yet: frame 12 x10 with `r_sceneOcclusion 0` first. A hashed per-instance pull (Fable's idea) must beat 24-bit depth at distance yet stay below real separations, so it can only resolve near ties |

**FPS item 1, part (a), coded** (5461829f): the cluster tile test reads a 16-byte sphere and a 4-byte kind per light
(built at load from the same values) instead of the 64-byte record: the same runs, order and tests, so the same lists.
The load also prints how many lights light surfaces and how many scatter in the haze, which decides part (b): static
subsets change each subgroup's run, and with it which tiles overflow to the grid, so (b) is bit-identical only where no
tile overflows (SCENECLUSTERS can show that first).

**Gates, fixed before running** (the pipeline session's, short; `fix_gates.sh`, one exclusive window after the owner's
play and the collision session's gate):

1. **The coplanar-tie check**: Watson's camera-locked frame 12 (and 24) in 10 fresh loads of the baseline (e6837414's
   build) with `r_sceneOcclusion 0`. If all ten agree, phase 2's partition is the likely cause of the two-state variant
   (it never appeared in this form), and the tie fix comes next with its own gate.
2. **The defect fixes' image gate**: make_hold.py's 32 frames, the baseline against the fixes build (`occ/dll/fixes1`:
   every fix above and 1(a)); bit-identical except where a fix is meant to change pixels (the normals on scaled or
   mirrored instances, counted at load): every changed pixel reported with where it is.
3. **FPS item 1(a)**: its image check is gate 2 (it claims bit-identity); timing A = the fixes without 1(a)
   (`occ/dll/fixes_noP2a`), B = with it: the slice with two clients taking turns (`alt_bench.py`), Watson one client
   at a time, A, B, A (`seq_bench.py`), both by 1h's rule (the median at most 0.15 ms worse, or inside A's own spread).

**Results** (exclusive window 01:09-01:29, `occ/fix_gates.log`; attribution runs after it in one normal slot):

| Gate | Result |
|---|---|
| 1. Coplanar ties | **Not confirmed.** Frame 24 identical in all ten loads; frame 12 in four variants, 0, 1, 17 and 18 px from run 1, each one level (`occ/fix_gate1.txt`). The 18 px set is the one order2 showed, here with phase 2 off: not the partition. No tie fix (the rule above). Recorded as its own open item: the first frames after a load land in one of a few states by timing, settled by frame 24 |
| 2. Defect fixes' images | 0 of 32 frames identical to the baseline (`occ/fix_gate2.txt`). Attribution: fixes1 without the normals commit is bit-identical on Watson's 26 frames, not on the slice (`occ/fix_gate2_nonormals.txt`); without the normals and the lamp-shadow rain commits, the slice's 7 frames are bit-identical too (`occ/fix_gate_norain_slice.txt`). So two fixes change pixels: the normals (Watson frame 180: 653,503 px, max 143; slice frame 10: 33,809 px, max 157) and the rain flag (slice frame 10: 4.0M px, max 247: the lamp faces that took the rain view's path on stack garbage lost casters, and the cyan door light leaked up the wall above the overhang) |
| 3. 1(a) | Images: alone on the slice, bit-identical (`occ/fix_gate_p2a_slice.txt`; SCENECLUSTERS 0 full tiles, 13 full clusters). Watson one client at a time (A B A2): B - A -0.06, +0.03, -0.10, -0.37 ms, A drift 0.01-0.58, **PASS**. The slice's alternation is not evidence this time: A's second and third passes ran 5.5 ms slower than its first (its spread 5 ms); its per-pass medians put the clusters at 0.538 against 0.473 ms |

**Committed** on the renderer branch (not pushed, not installed), as exactly the gated build (fixes1 without the normals
and rain commits): the review fixes as 9e2138b6, 1(a) as c8e44a43. The normals (1753ce22) and the rain fix (301cc736)
wait on `wip/p3-fixes` for the owner, with side-by-sides: `occ/normals_watson_f180.png`, `occ/normals_slice_f10.png`,
`occ/rainfix_slice_f10.png`. **Owner, 2026-11-01, after the three side-by-sides: "Yes, both"**: the rain fix
committed as 6d1b0b37, the normals as 767feff7 (renderer files only; not pushed, not installed); the branch's renderer
sources now equal the gated fixes1 build. The `tileFull` row above: the old code was not a race after all (one thread cleared it
before a barrier, then subgroups only wrote `true`); the new form is kept for its simpler reasoning, and it is
bit-identical.

## 1j. Scene v4: CP2077's hide distances, the view distance and distant lights (2026-11-01)

The pipeline's `cp_watson_scene-2` and `cp_watson_slice_scene-22` carry CP2077's hide distances: 1,508,464 of 1,610,399
instances (p10/p50/p90 31/104/403 m) and 39,053 of 41,398 lights (15/30/90 m), every light a shadow fade (10/10/15 m).

**The loader and the hides** (committed as cfd7bd3f): version 4 is read; instances past `hide x r_sceneHideScale` are culled
in every view; `scene_lightfade.comp` turns a light off past `hide x r_sceneLightHide`, fading over the last 15%, fades its
shadow by shadowFade + shadowRange, and leaves it out of the clusters. Images: scene-1 bit-identical to the build before
(32/32; the pass is inert on v3), scene-2 with both cvars 0 bit-identical to scene-1 (32/32). With the hides on, the
frames lose mostly distant light (the median light hide is 30 m), some far geometry.

**The view distance and the hides' cost** (`far_bench.py`: one Watson client at a time on the hub pack, which sets
distanceCull; four street views and one 60 m up; GPU frame medians in ms; `occ/far/far.txt`, sheet `occ/far_sheet.png`):

| Arm | lc_street_a | kabuki_street_b | ns_street_a | ns_street_b | elevated |
|---|---|---|---|---|---|
| r_distanceCull 6000, hides on | 8.90 | 12.54 | 6.69 | 10.33 | 10.41 |
| 20000, hides on | 9.16 | 12.83 | 6.73 | 10.94 | 11.64 |
| 40000, hides on | 9.09 | 13.33 | 6.50 | 11.01 | 12.21 |
| 20000, hides off | 10.45 | 14.02 | 7.52 | 12.49 | 16.38 |
| 20000, lights hidden only | 9.57 | 13.18 | 6.67 | 11.17 | 13.59 |
| 20000, instances hidden only | 10.17 | 13.53 | 7.41 | 12.00 | 13.75 |
| 20000, hides on (again) | 9.17 | 12.67 | 6.59 | 10.68 | 11.49 |

The lights' half is the clusters (2.43 against 1.17 ms). The last four arms overlapped the pipeline's CPU fetches; the
numbers are GPU timestamps, and the repeat matches the first 20000 arm within 0.26 ms. The owner kept 20000 (924 m).

**Distant-light stand-ins** (owner: "Yes, build them"; committed as 4d03d059): a light flagged allowDistantLight shows past
its hide as a plain point (no shadow, haze or box), fading in as the full light fades out, listed per light grid cell by
`scene_distant.comp` and added after the cluster loop, so the clusters keep the saving. Images: with
`r_sceneLightDistant 0` bit-identical to cfd7bd3f (32/32); on, the ns_street_b bus shelter and the elevated view's
highway lights come back (`occ/standin_*.png`, `occ/dc_*.png`). Cost against hides-on, one client at a time (A B A2, the
same stand-in code with the other work in progress switched off): -0.06, +0.03, +0.11, +0.45, +0.55 ms.

**Re-timed in one slot** (Watson tag 3, `cp_watson_scene-3`, r_distanceCull 20000, one client at a time, 2026-11-01
04:26-04:34; `occ/far3/far.txt`, `occ/far4/far.txt`, script `occ/slot_0400.sh`). Each run's first arm ran about 0.5 ms high
everywhere (warm-up), so each comparison is against the run's last arm. Absolute times on the committed build, with the
haze's lamp light:

| Arm | lc_street_a | kabuki_street_b | ns_street_a | ns_street_b | elevated |
|---|---|---|---|---|---|
| 4d03d059, stand-ins off (A2) | 8.54 | 12.09 | 6.43 | 10.26 | 10.98 |
| 4d03d059, stand-ins on | 8.48 | 11.97 | 6.48 | 10.70 | 11.33 |
| WIP c3783985 as it ships (base2) | 8.65 | 12.03 | 6.40 | 10.76 | 11.32 |
| its view cull's volume half off | 8.58 | 12.16 | 6.63 | 10.78 | 11.43 |
| two reflection rays a texel, an 8% share | 8.54 | 11.94 | 6.50 | 10.70 | 11.32 |

The stand-ins cost -0.06, -0.12, +0.05, +0.44, +0.35 ms, as on the WIP build. The volume half takes the haze pass from
0.371 to 0.270 ms (median over views); the frame moves within the noise. Two rays and 8% cost nothing measurable.

**Committed after the slot** (ported onto 7135bcf1, each commit its gated build: `occ/dll/vcport`, `occ/dll/ssr2`):
- ce3ffe21, the view cull's volume half, with the haze set's light fades written: r_sceneViewCull 1 against 0
  bit-identical on Watson scene-3 and the slice's scene-22 hold frames, haze on (32/32, `occ/vc_gate2_vcport.txt`);
  0 against 4d03d059 identical but the first frame's known two states (31/32, 18 px of one level,
  `occ/vc_gate2_base.txt`).
- 7fb72585, two reflection rays a texel and an 8% share by default (r_sceneSsrRays 2, r_sceneSsrBlend 0.08): set back to
  1 and 0.15, bit-identical to ce3ffe21 with the reflections on (32/32, `occ/ssr2_gate.txt`). At the owner's 001138 view
  the floor's frame-to-frame change falls from 0.49/0.60 to 0.32/0.32 (mean abs, 3 frames apart): the checker is
  fainter, not gone (`occ/ssr2_001138.png`).

**Tag 3 at the owner's 2026-11-01 reports** (scene-2 against scene-3, the same build, exact views; `occ/tag3_sheet.png`):
the trees (001059, 001432), lamp poles (000938, 001018), the billboard (001602), the shutters (001948) and the Tom's
Diner logo (002335) are in. The shutters are closed and static; if the collision map keeps the shop open, a player walks
through a shutter that looks shut.

**Found on the way:**
- A merge slip, not the view cull's logic: merging the view cull's volume half into the work-in-progress branch dropped
  the haze set's light-fade write (binding 13), so from that merge on the haze read no fades and scattered almost no
  lamp light (ns_street_b's cone under the overhang: `occ/cone_bisect.png` bisects it to the merge, `occ/cone_sub.png`
  shows the cone is haze light, `occ/cone_fixed.png` the fix, WIP c3783985). The committed trees never had it (the
  stand-ins were ported onto cfd7bd3f and gated there). The far-plane table above and the stand-ins' A/B ran on builds
  with the slip: each comparison is within one build, but the absolute times lack the haze's lamp light. Fixed; re-timed
  and committed above.
- The owner's floor "dither" (playtest 001138 and a later picture) is the wet ground's reflections, not the haze:
  reflections off removes it, the SSR filter off strengthens it; neither an r32f depth copy nor taking level ground as
  level removes it. It is the sampling noise of one jittered ray a half-size texel (interleaved gradient noise) settled
  at 15% a frame; two rays a texel or a 5% share reduce it (`occ/rb_band.png`). Two rays and 8% are now the default
  (7fb72585, above); the WIP's filter (r_sceneSsrDenoise) and flat-ground option stay uncommitted.
- The reflections never dropped: `screenshot_png` saves the next rendered frame, so a cvar set after it in the same frame
  is already in the shot. The "dropout" was `dither_look.cfg`'s haze-light-off shot, followed at once by
  `r_sceneReflect 0` (it equals the reflections-off shot within 4 levels). At 001138 on 7fb72585 (`occ/fogrefl`,
  `occ/drop`): a shot followed by `r_sceneReflect 0` loses the reflections, one followed by a wait keeps them; haze light
  off keeps them (floor 40.2 against 42.6); 60 shots of a still view, every frame and 2/4/8 apart, all within 0.2 of
  the median. In `playtest_look.cfg` and `dither_look.cfg` each shot shows the next setting's state; scripts now wait
  after a shot before changing anything.
- Corner windows (001739): the two panes are one instance but land in different room cells, with the grid in the mesh's
  axes or turned to the facade (`r_sceneDebug 11`/`12`, `occ/window_debug.png`). Next: read back their positions.

## 1k. Scene v5: CP2077's far proxies and the hide fade (2026-11-01)

The pipeline's `cp_watson_scene-4` carries CP2077's proxies: 57,954 of 1,672,325 instances have a near distance
(p5/p50/p95 16/55/150 m) and a far one (136/259/724 m); scene-3 had 1,614,123 instances. Evidence `occ/v5r/`, scripts
`occ/v5r_gate.sh` (build v5a), `occ/v5r2_gate.sh` and `occ/v5r3_gate.sh` (v5c), sheets `occ/v5r/sheets/`.

**Committed as d11bc509** (the gated build `occ/dll/v5c`, the same tree), not installed:
- Version 5 is read. The near distance takes the old flags word, which was always 0. Within `near x r_sceneHideScale`
  of the eye a proxy is not drawn in any view, the sun cascades included, as with the hide distance. So the middle and
  far cascades already take the proxies, and a full mesh past its hide casts nothing; no separate work was needed.
- `r_sceneHideFade` (1) dithers each crossing, in the eye's views; the cascades pop. A mesh fades out over the band
  before its hide distance, and a proxy fades in over the band before its near distance (the cull draws it from there).
  The band is 40 m, or half the distance when that is nearer. The fade needs a dither-only depth prepass (mode 7) while
  it is on; its cost is not measured yet.
- The static light shadows and the rain view take every full mesh and never a proxy. The light grid and the rain view
  keep to the full meshes' bounds; only the far clip covers the proxies.

**Why the fade ends at the near distance.** I paired each proxy with the large meshes whose centres lie inside it (at
least a third of its radius; `px_pair.py` in the scratchpad, 440,286 pairs):
- The overlap, hide minus near, is p5/p25/p50/p75 -8/31/82/191 m.
- Exactly 0 m, an exact swap, is the most common single value (7%), and 6.9% are short of 0.

v5a faded the proxy in after its near distance. That left a band at an exact swap where neither was fully drawn. Ending
the proxy's fade at its near distance makes the two dithers complementary at equal distances, with both drawn past
that, so only an overlap below 0 leaves a hole, as it does popped.

**Found on the way** (v5a, fixed in v5c):
- The light-shadow bake runs the cull with the per-frame receivers buffer unwritten, so neither the hide nor the near
  test applied there (after a reload, the previous scene's values would have). The proxies cast into the lamps'
  shadow maps: the bake took 39 s against 20, and a lamp's haze cone under the overpass darkened
  (`near_d00180_signed.png`).
- The proxies widened the instance bounds, which coarsened the light grid from 46x64x19 cells of 47 m to 50x64x19 of
  48 m and moved light everywhere a little.

**Image gate** (v5c; hold demo, camera-locked, exact; the walk demo; `far_look.py` live views 60/150/400 m up and
turned, on the hub pack):

| Check | Result |
|---|---|
| A v4 scene unchanged (v5a against v5c on scene-3, hides off) | 24/25 bit-identical; frame 12, 20 px of one level (the known first-frame variant) |
| Near field: scene-3 against scene-4 without its proxies (`r_sceneHideScale 1000`), lit | view A within 2 levels; views B and C differ only in window interiors (`near_c_d00048.png`, `near_c_d00180.png`) |
| The same as normals (`r_sceneDebug 2`, geometry alone) | windows only (`near_n_mask_*.png`): no near-field geometry changed; the room atlas moved with the material table (19,061 to 25,092) |
| Hides off (`r_sceneHideScale 0`): scene-4 with every proxy drawn | proxies everywhere, covering near detail (`near_c4_d00180.png`, `prox_a_d00048.png`), as expected with hides off |
| Fade off against on, as normals: geometry that turns to sky | hold frames at most 3,000 px (0.036%): small things fading with no proxy (hanging lamps, a sign) that used to pop (`holes_d00300.png`). Live far views: thin silhouette edges and windows, matched by as much "cover" (sub-pixel view differences between runs). No mesh-proxy gap found |

**The skyline** (`far_skyline.png`: scene-3 and scene-4 at r_distanceCull 20000 and 40000):
- At 20000, scene-4 adds proxy towers and long blocks on the horizon at Kabuki and Little China, and fills the view
  400 m up.
- At 40000, more far blocks come in, though some are full meshes that scene-3 shows at 40000 too.
- The proxies' own far distances limit the gain: 11.8% of them reach past 20000 (533 m), 1.6% past 40000 (1,067 m) and
  0.6% past 1.5 km. Of those 10 m across or more, the shares are 48%, 10.5% and 4.2%.
- From 400 m up at Kabuki, a pale band crosses the sky in every arm, scene-3 included; it predates this work.

**Timed in one slot** (2026-11-01 10:07-10:13, GAME_LOCK_EXCLUSIVE, the pipeline's levelchecks suspended and the
collision session held, the owner's apps running; d11bc509 on scene-4 and the hub pack; `occ/slot5/slot.txt`, script
`occ/slot_1015.sh`, tool `occ/slot_bench.py`). The method:
- Three loads, one client at a time: r_distanceCull 20000, 56250 (1.5 km), then 20000 again. The first load ran
  0.9-1.4 ms high everywhere (warm-up, as before), so the far plane is compared against the third.
- Inside each load the fade is toggled at every view, on, off, on again; the repeat is the load's own floor
  (median |on - on| 0.03-0.05 ms).

The fade's cost, on minus off, in GPU ms:

| Load | lc_street_a | kabuki_street_b | ns_street_a | ns_street_b | elevated | ns_b 150 m up | lc_a 400 m up | median |
|---|---|---|---|---|---|---|---|---|
| 20000 (first) | +0.02 | +0.56 | +0.11 | +0.36 | +0.41 | +0.38 | +0.16 | +0.36 |
| 56250 | +0.18 | +0.37 | +0.09 | +0.36 | +0.40 | +0.23 | +0.19 | +0.23 |
| 20000 (again) | +0.15 | +0.24 | -0.13 | +0.40 | +0.36 | +0.18 | -0.01 | +0.18 |

Most of it is not the prepass's fragment stage (scene depth +0.05); the lit pass takes about +0.2 ms. At kabuki_street_b
the sun shadow pass took +0.27 ms: while the fade is on, the cull drew a proxy from a band before its near distance in
every view, the cascades included, so through the overlap both it and the mesh cast.

**The cascades pop proxies at their near distance** (owner: "Yes, build it"; committed as 6b3a7b51, build `occ/dll/v5d`,
the same rd-vulkan tree): only the view the fade dithers draws a proxy from the band early.
- Images (`occ/v5d_gate.sh`, hold demo against d11bc509): the fade off is bit-identical (25/25). What the eye draws is
  unchanged: the normals are 24/25 identical, the other being view C's arrival frame (dithered small things only;
  normals carry no shadow). The fade on differs in faint shadow alone.
- Timed in a second slot (10:47-10:52; loads v5c, v5d, v5c, each toggling the fade; `occ/slot6/`, `occ/slot_v5d.sh`).
  With the fade on, v5d's shadow pass is 0.024 ms lighter on lc_street_a, ns_street_a and ns_street_b; with it off, the
  same as v5c. In v5c those three views' fade costs 0.019-0.026 ms of shadow, so that share is gone.
- Correction: kabuki_street_b's 0.27 ms was not the fade. Its shadow pass moves 0.2 ms between loads with the fade off
  as well (the far cascade's cached casters), and in this slot v5c's fade cost it +0.07 and -0.01 ms.
- The fade still costs about 0.2 ms a frame, the lit pass and the prepass. Run to run, the v5d load was noisier
  (in-load floor 0.36 ms against 0.09-0.10), so its frame totals say nothing further.

**The owner's playtest kit** (`cp_watson_hub_col-8`: worldspawn distanceCull 56250, residency on, d11bc509, scene-4),
loaded once in that slot as they play it, from a private home (`occ/kit8_fps.sh`): 88 fps at the spawn (GPU median
10.5 ms), 60 fps at the rooftop start -7275 -44812 420 (15.4 ms), with the join menu still open over the view. The
smoke's ~4 server fps came from sharing the GPU with a look run.

The far plane at 1.5 km against 20000 (the fade on, both on-windows pooled): the streets +0.07 to +0.21 ms, 60 m up
+0.16, 150 m up +0.28, and **400 m up +2.31 ms** (10.27 to 12.57; the lit pass +2.0, the depth pass +0.19). The cost
appears only where the view reaches past 533 m. The far plane is the owner's decision.


## 1l. The playtest crash, review round 2, three FPS checks and three playtest reports (2026-11-01, afternoon)

Evidence `occ/repro/`, `occ/v5r/` (the gate), `occ/slot7/` (timing), `occ/rl/` (the reports' views), sheets `occ/rl_*.png`.

**The crash at 11:20:41 (kit col-9, 6b3a7b51): the textures' descriptor pool ran out.** Committed as 04db99fa.
- Symbols from a rebuild of the same source with a PDB: its code matched the kit's DLL but for 6 bytes. The stack, inner
  first: `vk_update_descriptor_set` (vk_image.cpp) < `vk_create_image` < `R_LoadDDSImage` < `ParseStage`. So a shader
  loaded a new DDS texture during the owner's model switch.
- Why: `vk.descriptor_pool` is sized at init for MAX_DRAWIMAGES (4,096) textures, about 13,300 sets with the PBR extras,
  while the image list grows without limit. Scene-4's kit loads 12,872 images, leaving room for about 470, roughly five
  hero models; the owner hit it on the fourth. `VK_CHECK` only logged the failed allocation, so the next
  `vkUpdateDescriptorSets` got a null set: a read of null + 0x108 inside the driver, as the dump shows.
- Fix: the sets that are never freed one by one (textures, generated normal maps, probe blends) come from overflow pools
  of 4,096 sets once the pool is full, reset and destroyed with it; past 32 of those, a clear error.
- Reproduced on a copy of the kit (`occ/repro_pool.sh`, 40 hero models by console): the shipped DLL crashed on the sixth
  switch at the owner's driver offset; the fix logged "descriptor pool full at 13341 images: overflow pool 1" there and
  went on.
- A second, separate limit: at the fifteenth model the fixed DLL ran out of device memory ("GPU memory heap overflow"),
  a clean drop to the menu, not a crash. Hero textures accumulate during play, and another session's client shared the
  GPU. Three or four switches are far from it; a long session in the model picker at 1.5 km is not.

**Review round 2 (both reviewers): committed as f810f4f4** with F3 below; the reports are in
`look/review-20261101b/`.
- A1: occlusion's phase 2 compared the spare view slot with the eye's index, so it never drew a proxy in its fade band:
  a proxy uncovered in its band came a frame late. Phase 2 is always the eye's.
- A2: the far cascade kept across frames decided hides and near distances from the eye of the frame it was drawn in.
  It now uses its grid cell's centre (and its LODs too), so what it keeps matches one point.
- A3: a light's shadow faded only if the light also had a hide distance; 2,345 of Watson's have none.
- A4: a proxy whose hide band starts before its near band ends jumped in coverage. Counted first: 234 of scene-4's
  57,954. It now draws the share both leave.
- A5: haze-only lights could fill a cell's distant stand-in places; only lights that light surfaces take one now.
- A6: near and hide distances are validated at load (negative, -0.0 or not finite read as none, with a warning).
  Scene-4 has none.
- Also in f810f4f4, off by default: `r_sceneProxyDebug` (1 no proxies, 2 every one at any distance) and
  `r_sceneDebug` 13/14 (the world position modulo 256 and 65,536 game units, 8 exact bits a channel: two PNG shots give
  a pixel's position to about a unit).
- Gate (hold and walk demos against 6b3a7b51; the merge alone was bit-identical first, 25/25): normals 24/25
  bit-identical, the other view C's arrival frame as in every run; lit frames within 6 levels in small patches (A2, A3,
  A5); walk normals 16/16.

**Three FPS checks in one exclusive slot** (12:36-12:39; one load of scene-4 on the col-9 hub pack at r_distanceCull
56250, toggled view by view; the floor, base against base again, median 0.03 ms, at most 0.10). GPU ms against base:

| Toggle | lc_street_a | kabuki_street_b | ns_street_a | ns_street_b | elevated | 150 m up | 400 m up | rooftop | median |
|---|---|---|---|---|---|---|---|---|---|
| F3: `r_sceneLitEqual 1` | -0.05 | -0.28 | +0.04 | -0.43 | -0.37 | -0.11 | -0.08 | -0.16 | -0.14 |
| H1 bound: `r_sceneCullPixels 3` | -0.10 | +0.03 | +0.03 | -0.14 | -0.07 | -0.03 | -0.53 | -0.00 | -0.05 |
| H1 bound: `r_sceneCullPixels 6` | -0.03 | +0.06 | -0.02 | +0.06 | -0.20 | -0.24 | -0.91 | +0.04 | -0.03 |
| `r_sceneLodBias 0.5` | -0.44 | -0.99 | -0.17 | -0.58 | -0.73 | -0.40 | -0.31 | -0.84 | -0.51 |

- F3, the GPU-driven lit pass at depth EQUAL: the lit pass alone, no view slower, so the coarse depth cull survives;
  bit-identical images (25/25). The owner made it the default (f810f4f4).
- H1, a distance-weighted size cull: worth up to 0.9 ms only from 400 m up; the streets and the rooftop sit in the
  floor. Low priority.
- LOD bias 0.5, every LOD a step coarser: the biggest lever, on every view, split between the lit pass (up to -0.5),
  the depth pass (up to -0.3) and the sun shadows (-0.45 at kabuki_street_b, -0.52 at the rooftop). It costs detail
  everywhere; coarser LODs in the cascades alone (`r_sceneShadowLod`) and a far-only LOD floor are the narrower forms.
  Both wait for the owner.

**Three of the owner's col-9 reports, at their exact views** (`occ/report_look.cfg`; root causes from the pipeline):

| Report | What the renderer showed | Root cause |
|---|---|---|
| An ad black in patches at mid distance, fine up close | With proxies off the poster and the lit windows show; with the fade off it stays covered; the material view shows a proxy panel over the poster. At about 119 m the detail and its proxy are both drawn, and the proxy's coarse shell lies in front of the inset detail | CP2077 unloads a node past its record's streaming distance, whatever its hide; we used the hide alone, so detail outlived its proxy's near. Tag 5 caps each hide at the streaming distance |
| Antennas and crane tops floating over the roofs | Every proxy drawn at any distance changes nothing: no proxy carries the masts | The masts, crane arms and ropes are in CP2077's streaming level 5; only levels 0-3 were exported. Tag 5 exports 0-7 |
| A hole in the crossroads | The crosshair is sky. The edge at road height, from views 13/14: CP2077 x -1507.8..-1398.0, y 928.0..971.4, z 22-23.5 (about 110 x 43 m). Ground at about 0 m shows through its east part; nothing under its west part | The deck is one level 5 mesh (wlc_01_crossroad_02, sector exterior_-1_0_0_5); the owner's crosshair traces miss too, so collision lacked it as well |

A depth push for proxies (`r_sceneProxyPush`, metres farther along the eye's ray, off) is a safety net for the first
report; it stays uncommitted until tag 5, which removes most of the overlap, shows whether it is still needed.

## 1m. Every review item worked through: defects, decals, and ~1.5 ms a frame (2026-11-01, evening)

The owner, 15:55: "work through all the feedback items from fable and astra"; the tracker is
`look/review-20261101b/OPEN_ITEMS.md`. The renderer's items, in order of measured value. Builds `occ/dll/v6a`..`v6i`.
The owner said "Yes to all": **committed as f847bd65** (the gated candidate `wip/v8` 99c8068a, `occ/dll/v6i`, the same
rd-vulkan tree, compiled again at commit time to the same DLL); not installed.

**Defects** (each gated bit-identical: hold and walk demos against the build before):
- R-a: the light box record is read as words, so the distant flag is never a float load.
- Round 1, re-audited against today's code. D7 (subgroups) was fixed already; fixed now:
  - D5: a split frame's first submit waits for the swapchain acquire when it holds that image's first-use transition.
  - D6: the cached device commands are fetched per device and cleared with the scene.
  - D8: the SSR jitter is seeded by the game clock.
- Still open:
  - D1 (coplanar ties across occlusion's phases): it is not the frame-12 variant, and its fix changes images.
  - D4 (the 1.3 GB load peak): a refactor, after the speed items.

**The door's "z-fighting"** (owner, col-9; measured offline from scene-5's geometry, `door_gap.py`):
- The wall passes through the door's slab: 2.2 mm behind its front face, 32 mm in front of its back face.
- Through the door's openings you see the bricks, because the openings hold a CP2077 mesh decal
  (mesh_decal_parallax: DiffuseAlpha 0.7, a normal, a height, DepthThreshold 0.5) that we draw as a 0.7 blend.
- In CP2077 such decals project their albedo and normals onto the surface behind them. 1,101 of scene-5's materials are
  decals of this kind.
- **R-c, the fix: DBuffer decals** (a decal pass after the prepass writing albedo and normal into two screen targets
  within the depth threshold, read by the lit pass). Estimated 0.2-0.4 ms and 66 MB at 4K, 3-5 days. The owner
  approved it; the converter carries the decal fields first.

**Speed** (exclusive slots, one load toggled view by view unless noted; GPU ms, median over the eight views; floor
0.01-0.04 ms):

| Item | Result | Images | State |
|---|---|---|---|
| F-4: a light's 16-byte sphere tested before the rest of its record (lit pass) | -0.48 (up to -1.19 at 400 m up), A/B loads | bit-identical | committed (f847bd65) |
| List packing a workgroup per 1024 commands (was one per view) | -0.10 | bit-identical | committed, old path `r_sceneCompactBlocks 0` |
| F-6: the cluster tiles walk the lights live and in view, listed once a frame | -0.91 (cluster build 1.2 to 0.15-0.3 ms) | bit-identical, no tile overflow | committed, old path `r_sceneClusterCull 0` |
| F-6's wide slice test | +0.10 (slower) | | dropped |
| F-9: middle cascade kept like the far one (`r_sunShadowMidCache`) | still views -0.24 (lc_a -0.70, kabuki_b -1.00, rooftop -1.93; +0.10 where there is no sun shadow); walking demo frame about -1.0, shadow pass 1.31 to 0.43 | mid-range shadows coarser (`occ/ml_sheet.png`) | committed as a switch, off; its layer exists only when on; the owner said yes to it as the default (next commit, after its gate) |
| F-1: shadow LOD 0.25 (from 0.5) | up to -0.45 on shadow-heavy views, median -0.04 | almost no change (`occ/ll_sheet.png`) | the owner said yes as the default (next commit, after its gate) |
| F-2: far-only LOD floor | at most -0.28, only 400 m up | | dropped |
| H1: distance-weighted size cull | -0.5 to -0.9 only from 400 m up | | low |
| F-7: cull tile skip | est. 0.1-0.2 (scene-5's 91 tiles are 256 m; the eye keeps 26-93% of instances) | | not done |
| F-3: the fade computed in the cull | est. 0.05-0.1 after F-4 | | not done |

Together the commit takes **about 1.5 ms a frame off every view** with identical images. F-9 and F-1 would add up
to about 2 ms more on shadow-heavy views, at their look costs.

A correction made on the way: on the walking slot I first read "scene pack, clusters" as 0.9 ms of packing and 0.3 ms of
clusters. The two run side by side, so the cluster build was hiding most of the packing. The packing A/B (-0.10)
showed it, and F-6 then took the 0.9.

Also measured: the light loop is the lit pass's biggest cost (all lights off -1.85 median, up to -3.80 at 400 m up), the
target of F-4 and of any later light work.

## 1n. The defaults and the mesh decals, committed (2026-11-01, evening)

**Defaults** (f7680b24, the owner's yes): `r_sceneShadowLod` 0.25 (F-1) and `r_sunShadowMidCache` 1 (F-9), gated as
v6j (the same rd-vulkan tree).
- The hold demo against f847bd65 changes only sun-shadow texels: 0.03-5% of pixels, max 94.
- The both-on look matches the two gated sheets (`occ/def_sheet.png`).

**Mesh decals, R-c** (df3a37b3, the owner's yes): CP2077's mesh_decal* materials now project their albedo and normal
onto the surface behind them, as section 1m scoped.
- The converter marks them (flags 0x40; tint.a DiffuseAlpha; window DepthThreshold in metres and NormalAlpha), and
  the renderer leaves them out of every draw list.
- After the eye's prepass, inside occlusion's pass split (or one of its own when occlusion is off), a pass draws the
  eye's culled decal commands into three screen targets (the DBuffer): albedo and coverage, normal and weight, and the
  receiver's view depth.
- Each decal fragment keeps only what the prepass holds within its threshold behind it.
- The lit pass (both paths) mixes the DBuffer in before lighting, only on the surface whose depth it recorded. A
  surface occlusion's second phase reveals gets no decals that frame, and never takes the decals of the one behind.
- `r_sceneDecals 0`, read at map load, draws them as blends again.
- 83 MB at 4K; 1 x 1 targets when a scene has no decals.

Gated as v6k (the same rd-vulkan tree):

| Check | Result |
|---|---|
| Identity: scene-4's hold demo against v6j (no decal materials) | 25 of 25 frames identical |
| The pipeline's door scene (tile -6_4, 500 decal materials), deterministic capture, decals on vs off | lit 36,436 / 46,966 px change, 0 outside the decal sites; the door shows its grille, grooves and handle as CP2077 draws them, where the blends drew black bars (`occ/rc/rc_door_sheet.png`) |
| Timing, four loads interleaved (`occ/rc_time.sh`) | +0.03 to +0.07 ms a frame: the pass ~0.07 ms flat (mostly clearing three 4K targets), the lit reads ~0.01 ms, no extra split |

Still open:
- A Watson-wide look and timing need a full scene rebuilt with the decal records (scene-6, after the instancing work).
- Decal height (parallax), roughness and MSAA-exact edges are left for a later phase.
- An optional trim: skip the pass where no decal is in view.

## 2. The renderer's plane copy: remapped

**Before.** `R_LoadPlanes` (tr_bsp.cpp) copied the BSP's whole plane lump into the hunk, at twice its count (a Quake 3
leftover: nothing in the renderer reads `planeNum ^ 1`). Only three things in the renderer read map planes: the nodes'
split planes (`R_PointInLeaf`, the dynamic light walk, `R_MarkFragments`, all through `node->plane`) and the fog
brushes' first six sides and visible side (`R_LoadFogs`). A scene map's planes are its collision brushes' sides,
millions of them, against a few thousand nodes (`bsp_planes.py`):

| Map | Planes | Nodes | Distinct node planes | Copy before | After |
|---|---|---|---|---|---|
| Kabuki map-22 | 2,301,222 | 1,647 | 38 | 87.8 MB | 760 B |
| Slice-20 | 5,589,337 | 28,139 | 13,799 | 213.2 MB | 0.3 MB |

**The change** (`codemp/rd-vulkan/tr_bsp.cpp`, committed 2026-10-31 on the owner's OK; not installed): `R_LoadNodesAndLeafs` collects the planes its nodes use
(sorted, unique), copies only those and points each node at its copy; `R_LoadFogs` reads its few planes straight from
the file's lump (`R_FilePlane`, which range-checks the index); `R_LoadPlanes` is gone. `worldData.numplanes` is now the
node plane count; nothing else reads it. Sub-BSPs (`misc_bsp`) take the same path.

**Memory, gated** (`mem_probe.py`, the zone's own total with the map loaded, 32-bit, A against B):

| Map | Zone, A | Zone, B | Drop | Expected (count x 40 B - node planes x 20 B) | Private bytes |
|---|---|---|---|---|---|
| Kabuki | 360,468,036 | 268,419,916 | 92,048,120 B (87.8 MB) | 92,048,120 B | 995 -> 907 MB |
| Slice-20 | 669,511,426 | 446,213,924 | 223,297,502 B (213.0 MB) | 223,297,500 B | 1,332 -> 1,111 MB |

The drop is the expected amount to the byte on Kabuki (two bytes of allocation rounding on the slice). The copy grew
with the collision map's planes, so it would have grown with Watson's; now it grows with the nodes only.

**Images, gated** (`td2.py --shots`, fixed timedemo frames at 3840x2160, the owner's openjk.cfg; `cmp_shots.py`). The
timedemo camera repeats exactly, but the frame is not bit-identical run to run (the saber's light and cloth, the
reflections' frame counter, the haze), so each side was run twice and A against B is read against A against A:

| Pair | Kabuki lap (demo frames 150, 500, 900) | Slice views (frames 55-195, first person, scene dlights and reflections off) |
|---|---|---|
| A against A (32-bit) | 87.0-96.5 dB, no pixel off by more than 4 | 43.6-59.7 dB, 0.13-0.86% of pixels off by more than 4 |
| B against B (32-bit) | - | 41.1-68.7 dB, 0.01-1.09% |
| A against B, 32-bit | 87.7-94.4 dB, none off by more than 4 | 40.8-62.6 dB, 0.05-1.08% |
| A against B, 64-bit | 66.6-101.2 dB, at most 0.005% | 49.8-67.5 dB, 0.01-0.78% |

A against B sits inside each map's own run-to-run band on both builds. The slice's first view (frame 20, the first
frames after the load, the exposure settling) differs by 18-30 dB in every pair, A against A included. In third
person with the owner's saber lit, the slice's run-to-run noise is far larger (17-49 dB, A against A and A against B
alike), which is why the clean pair was run. Not bit-identical, so this is a noise-band gate, not a proof; the remap
copies the same plane values the old code did (same conversion, same `PlaneTypeForNormal`, same sign bits), so the
nodes' planes are identical by construction.

## 3. The 64-bit client's extra memory: explained

Phase M measured the 64-bit client committing about 3 GB more private memory than the 32-bit one on the same scene
(section 7 of the Watson page: 3.9 against 0.9 GB on Kabuki; 5.9 against 1.9 GB on the slice). The engine's own
allocations do not explain it: the zone with Kabuki loaded is 370 MB on 64-bit against 344 MB on 32-bit (wider
pointers and structures, +26 MB; the slice +101 MB, most of it the collision map's).

**The cause is the NVIDIA Vulkan driver.** In a 64-bit process every device-local allocation is matched by the same
amount of private, write-combined (`PAGE_READWRITE | PAGE_WRITECOMBINE`) committed memory that is never touched, so
it is commit charge, not RAM. The 32-bit driver does not do this.

- **Isolated** (`vkcommit.py`: Python, ctypes on vulkan-1.dll, nothing else in the process): 16 device-local
  allocations of 32 MB (memory type 1, the one the renderer's `vk_find_memory_type( DEVICE_LOCAL )` picks) raised
  private bytes from 72 to 585 MB, the write-combined share from 12 to 524 MB; four of 256 MB, 72 to 1,098 MB. The
  working set stayed at 63 MB throughout. Freeing them left 236 MB committed (the driver's cache).
- **In the client** (`mem_probe.py`: `VirtualQueryEx` over the process at the menu, the map loaded, a few seconds of
  view, back at the menu; the GPU's per-process dedicated memory from Windows' counters; zone totals):

| Kabuki map-22 + scene-23, map loaded | 32-bit | 64-bit |
|---|---|---|
| Private bytes | 995 MB | 4,568 MB |
| of which write-combined | 102 MB | 3,656 MB |
| Working set | 594 MB | 779 MB |
| GPU dedicated (the process's VRAM) | 3,582 MB | 3,576 MB |
| Zone (engine allocations) | 344 MB | 370 MB |

| Slice-20, map loaded | 32-bit | 64-bit |
|---|---|---|
| Private bytes | 1,332 MB | 6,448 MB |
| of which write-combined | 102 MB | 5,131 MB |
| Working set | 852 MB | 1,124 MB |
| GPU dedicated | 5,045 MB | 5,043 MB |
| Zone | 638 MB | 739 MB |

The 64-bit client's write-combined commit tracks its VRAM (3,656 against 3,576 MB; 5,131 against 5,043 MB), and the
32 MB blocks in it are the renderer's image chunks (`IMAGE_CHUNK_SIZE`). At the main menu, before any map, it is
already 990 MB against 70 MB on 32-bit (957 MB of VRAM: the menu's images and targets). Leaving the map releases all
of it. Both drivers expose the same memory types (`vulkaninfo`, 32 and 64-bit: no Resizable BAR, a 214 MB BAR heap),
so the renderer chooses the same memory on both builds.

**What it means.** Nothing to fix in the engine: the pages are never resident. It does count against the system's
commit limit (RAM plus page file: 96.7 GB on this machine, 18.9 GB of it free at 07:20 today with the other sessions'
processes), so a 64-bit Watson client commits about its VRAM again on top of its working set. With the district's
scene at about 5.5 GB of VRAM plus targets, budget roughly 6-8 GB of commit for the GPU side and judge resident
memory by the working set. Phase M's "peak private" numbers for the 64-bit client measure VRAM, not RAM.

## 4. Tools and traps

| What | Where (`.codex-research/levelport/look/watson-p3-20261031/`) |
|---|---|
| Per-view profile: GPU passes, CPU frame, lit-pass invocations, one cvar changed at a time | `prof_views.py`, `prof_locked.sh`, `prof_rest_locked.sh` (`prof_chain2.sh`, `prof_chain3.sh` ran them); results `prof/`; read with `summ.py` (a line a window) or `passes.py` (every pass) |
| Memory by type and protection, zone tags, VRAM, at markers | `mem_probe.py`, `vmq.py`, `run_mem3.sh` |
| Device-local allocation against process commit, alone | `vkcommit.py` |
| Timedemo shots for any map and either build | `td2.py`, `run_par*.sh`, `cmp_shots.py`; the slice's demo `par/slice_views.dm_26` (`rec_slice.cfg`) |
| Plane counts and the remap's expected saving | `bsp_planes.py` |

- **D: was saturated** (queue length about 22, 17 MB/s of random reads shared with other sessions): the slice's
  4,699 images took over 25 minutes to load and two probes timed out. Packs copied to `%LOCALAPPDATA%/xenjk-p3/packs`
  and homes on C:: the slice then loads in about 40 s.
- **`stop` is not a command here**: JKA's is `stoprecord`. `rec_slice.cfg`'s demo ran to the client's exit (203 frames,
  six views), still usable.
- **The owner's openjk.cfg plays third person** with Eve 2B and her saber: timedemo shots then carry the saber's light
  and cloth, which differ run to run. Parity on the slice wants `cg_thirdPerson 0`, `r_sceneDlights 0`,
  `r_sceneReflect 0`.
- **Editing a shell script while bash runs it** shifts what bash reads next (it reads the file as it goes); Python's
  text mode also turned LF scripts into CRLF. Copy the script and edit the copy.
- **Killed lock holders leave slot files** (`game.lock.N`, 30 minutes to go stale): remove your own after killing a
  chain. The other side of the same rule: a holder that runs longer than 30 minutes (the collision session's drop
  check) loses its slot, so `GAME_LOCK_EXCLUSIVE` can start beside a running levelcheck; `prof_locked.sh` therefore
  also waits for no levelcheck, q3map2, WolvenKit or blender and a quiet CPU (it waited 10 minutes the first time).
- **The exclusive lock starves**: 32 minutes for the first run with three sessions' clients cycling. Take it once for a
  whole series (`prof_rest_locked.sh`), not per run.
- **`screenshotJPEG` is not a command here** (`screenshot_png` is): `prof_views.py`'s view shots were not written; the
  views are phase M's positions, and their timings repeat phase M's.
- **Client start-up reads the game directory on D:**: with D: saturated, the first profile run spent 11 minutes before
  `R_Init` even with the packs on C:.

## 5. Next

1. **Occlusion culling: committed** (2873dbc8, section 1b); **the tiled loader: committed** with this report (section
   1c). Neither installed.
2. **The per-LOD visible ranges: committed** (a32d66cf, section 1d), not installed.
3. **Watson's street views and laps: measured** (section 1e): 10.7-16.7 ms on its streets, scene only; no view
   reaches 120 fps.
4. **The haze, profiled** (section 1f): Watson's 606 haze volumes passed the shader's 256-volume mask, so every
   froxel tested them all: 4.1-4.3 ms of its 4.2-4.5.
5. **Instances drawn in instance order: committed** (cf415962, section 1g): the baseline's tie flicker fixed; frames
   reproduce bit for bit; 0.06 ms on Watson. **The volume mask at 1,024: committed** on top of it (section 1g):
   Watson about 4.1 ms faster on every view. Neither installed.
6. **Open: frame 12's two-state variant** (section 1g): the first shot after a load, 2 of 10 order2 runs outside a
   window. Next, after the view cull: frame 12 alone in an exclusive window, 20 runs each of order1 and order2
   (the pipeline session's plan; it may be load-time residency, not draw order).
7. **The view cull** (lights and haze volumes culled to the eye's frustum before the clusters): built on the old base;
   rebased onto 5, then gated.
8. The plane remap is committed (owner, 2026-10-31: "ok yes to both"), not installed; it reaches the game with the
   next renderer install.
9. Owner decision still open: the far plane (`distanceCull`; Fable: about 860 m with the level 4-6 proxies beyond).
   Every number here is of views clipped at 277 m.
10. **Scene v5, the far proxies and the hide fade: committed** (d11bc509, section 1k), not installed. Timed: the
    fade about +0.2 ms (the lit pass and the prepass; the cascades' small share removed by 6b3a7b51); the far plane at
    1.5 km +0.07-0.28 ms on the streets, +2.31 ms 400 m up. The owner chose 1.5 km (hub col-8): 88 fps at its spawn,
    60 on the rooftop.
11. **The playtest crash fixed** (04db99fa: overflow descriptor pools) and **review round 2 committed** (f810f4f4:
    A1-A6, the lit pass at depth EQUAL by default, -0.14 ms), section 1l; not installed. Waiting for the owner: coarser
    LODs in the cascades alone (LOD bias 0.5 everywhere measured -0.51 ms), and `r_sceneProxyPush` on tag 5.
12. **Every review item worked through** (section 1l's batch, then 1m): the defects fixed, the door traced to CP2077's
    mesh decals (R-c, DBuffer decals, scoped), and about 1.5 ms a frame off every view with identical images (F-4, the
    packing, F-6), committed as f847bd65. The owner said yes to F-9 and F-1 as defaults (their own commit, after a gate)
    and to R-c (DBuffer decals), next.
13. **The defaults and the mesh decals committed** (section 1n): F-1 and F-9 as defaults (f7680b24); CP2077's mesh
    decals into a DBuffer (R-c, df3a37b3), +0.03 to +0.07 ms; not installed. Next: a Watson-wide look on scene-6 with
    the decal records.
