# Kabuki roundabout (Cyberpunk 2077) handoff (2026-10-27)

**Read this to resume the Kabuki conversion.** Its rendering design is in [levelport-scene.md](levelport-scene.md),
the converter recipe in [tools/levelport/README.md](../tools/levelport/README.md), and the dated build and
measurement log in `.codex-research/levelport/cp77/kabuki/NOTES.md`. Converted game data and the reference images
are local only: never committed, packaged or published.

The whole-district follow-on (Watson, 2026-10-30) is scoped in [levelport-watson.md](levelport-watson.md).

## Resume here

- **Current (2026-10-30 08:00): read [levelport-watson.md](levelport-watson.md) "Resume here" first.** Kabuki since
  the state below: playtest-2 fixes and the traffic signals with their sounds committed (6eac80aa, 9c7146d5, 02f92347,
  3b14e1c2) and installed (Install 47 packs: map-22, scene-23, the sound pack; Install 49: the GameData renderer and
  cgame, which `Play_Kabuki.bat` now uses). Built, not installed: map-23 + scene-24 (default quest ranges only, the
  patched q3map2: 127 s to compile). The converter changes since (default quest ranges, ep1 sectors, the patched
  q3map2, the area recipe and level checks) are in 2302af54 and 064d37ff. The bullets below are the 2026-10-29 state.

- **State (2026-10-29):** committed through `312e5bb2`: the lamp shadows (`3f2f243c`), the linear-light grade and
  remaining features (`54b00926`, scene-19), the far cascade's cache (`a16c446b`), then the lossless 240 fps work
  (`d30ea8f3` renderer, `312e5bb2` tools; see "240 fps at 4K"), then the owner's playtest fixes (see "Owner playtest
  (2026-10-29)"): `107893cc` rd-vulkan (the scene's dynamic lights and reflection mips, timed free; the haze's
  per-tile volume culling, -0.27 ms at 4K, lossless) and `66ae4164` the converter (embedded mlsetups, terrain, finer
  collision, devices: the traffic lights' poles, scene-21, map-17) with the docs. Nothing is pushed or installed. The
  owner holds commits; ask before committing.
- **The owner plays it** with `game_directory/Jedi Academy/GameData/Play_Kabuki.bat`: map `cp_kabuki-18` (since
  2026-10-29 15:00: stairs and slopes collide as drawn; map-17 07:20, map-16 before), scene `cp_kabuki_scene-21` (the
  traffic poles; scene-20 before; both hard-linked into
  `GameData/base` as `zz_cp_kabuki*.pk3`) and the dev renderer copied to `.codex-research/kabuki-play/` (see its
  README.txt; with the haze volume culling). Changing what it loads: swap those links, never rebuild a pk3
  in place.
- **Next:** the owner's playtest feedback on map-16/scene-20 first. Then, the owner's pick:
  1. Open data question: the hole at screenshot 42-34 (terrain added; not seen at the owner's spot: ask for
     `/viewpos`). Settled 2026-10-29: traffic lights fixed (their poles, scene-21/map-17), walkway ads stay.
  2. Prop collision with real shapes at a compile cost q3map2 can take (a few brushes per prop, not one per triangle).
  3. Performance, Codex Astra's ranked list (`.codex-research/levelport/cp77/kabuki/astra-perf-20261029.md`): the haze
     on the async queue, fog-volume candidate culling and an exact puddle-noise gradient cache are the quick ones;
     occlusion with a depth-only seed pass before the main pass is the largest (0.2-0.8 ms, low confidence). Astra
     sees no lossless path to 4.17 ms; about 1-2 ms is left. Done since: the volume culling (-0.27 ms); bounds and
     what is left are at the end of "240 fps at 4K".
- **On hold by the owner:** the brightness rebalance (`r_sceneFogStart` is 150 m by the owner's pick; the frame is
  darker than reference A's in the middle and bottom bands since), people and cars.
- **To look:** the commands in "Rebuild and look", with `cp_kabuki-16` and `cp_kabuki_scene-20`, from the clean
  camera. Compare shots A and B with `look_stats.py` against the references. A run-around video:
  `look/runvid2.py`; frame rate: `look/timedemo.py` (see "Facts that cost time").

## Where it stands

The owner asked for the Kabuki market (the roundabout) in xenjk. They then chose a design built for its density
over the BSP: "go for the new way not trying to shove this into the old". The map has two layers:

1. **A gameplay BSP:** collision, spawns, the boundary and the light grid.
2. **A render scene** drawn only by rd-vulkan:
   - 61,771 instanced placements, with all their LODs;
   - 2,023 of the game's own lights, the moon's shadow cascades, and static shadow maps for the 308 lights the
     game shadows;
   - glow, and haze with the lights scattered in it (in shafts where they cast shadows), the game's fog volumes and
     steam columns;
   - wet ground with screen-space reflections;
   - the ad screens (stand-ins varied per billboard, their lights in the ads' colours), masked glows (lanterns,
     lightbox faces), and rooms behind the windows;
   - the game's own colour grade over the frame.

**Commits** on `renderer/pbr-materials` (not on master, not pushed):

| Commit | What |
|---|---|
| `e314e00e` | rd-vulkan render scenes |
| `e6beef46` | the converter |
| `f5aa951d` | contrast: lamp glows, light scale |
| `63723eed` | screens and haze sky |
| `50e513b6` | this handoff, and `look_stats.py` |
| `9ca27bb1` | light in the haze, wet ground with settled reflections, window rooms |
| `3f2f243c` | the colour grade, the reference A pass, lamp shadows |

**`9ca27bb1` (2026-10-27), in the order built:**
1. **Light in the haze.** A froxel volume in rd-vulkan (`scene_fog.comp`, `scene_sky.vert`), scene format version 3
   (each light's `scaleVolFog`, fog-only lights kept), and the converter and docs to match.
2. **Wet ground.** Wet, glossy up-facing surfaces with puddles, sized light highlights, and screen-space reflections
   traced at half resolution and settled over frames (`scene_capture.comp`, `scene_ssr.comp`, a hook in
   `vk_one_resolve_pass`).
3. **Windows.** Rooms behind the glass from the source's own window parameters (`windowRoom()` in `scene.frag`; the
   material record's window fields; scene-10, a full rebuild).

**`3f2f243c` (2026-10-27):**
4. **Colour grade.** The game's LUT as `maps/<map>.xlut` and a full-frame grade pass in rd-vulkan
   (`scene_grade.vert`/`.frag`, `vk_scene_grade` after the post 1x blends); lamps and other light components kept out
   of the haze (`useInFog`); the haze and sky colours set before the grade; `look.py` turns the spawn shield off;
   the scene build lists its materials (`materials.json`, for `r_sceneDebug 8`).
5. **Reference A pass.** A clean camera for shot A; wide-spread lights as cones; the haze's warm albedo; the
   source's fog volumes and steam columns in the haze (`maps/<map>.xfog`); masked glows; stand-in ads per placement and
   ad lights in their ads' colours; neon at its own EmissiveEV; wet roughness; the sky and grade exposure recalibrated.
6. **Lamp shadows.** Static shadow maps for the source's shadow-casting lights, rendered at load into a depth
   atlas and read by `scene.frag` and `scene_fog.comp`; the converter flags those lights (type bit 2).

Its files: `codemp/rd-vulkan/` (`vk_scene.cpp`, `vk_local.h`, `tr_backend.cpp`; `shaders/glsl/scene/scene.frag`,
`scene_fog.comp` and the new `scene_grade.vert` and `scene_grade.frag`; `shaders/spirv/scene_shader_data.c`),
`tools/levelport/` (`cp2gltf.py`, `cp77mat.py`, `look.py`, `README.md`), `tools/scene_spirv.py`, and the docs. The
untracked `zips/` folder is not ours.

**Not installed.** It runs from the dev renderer DLL
(`.codex-research/renderer-bench/builds/xenjk-vk-dev/RelWithDebInfo/rd-vulkan_x86.dll`) through `look.py`.

**Current builds** (`.codex-research/levelport/build/`):

| Build | What |
|---|---|
| `cp_kabuki_scene-21/zz_cp_kabuki_scene.pk3` | render scene (2026-10-29): scene-20 plus the device entities (`worldDeviceNode`, destruction meshes): the traffic lights' poles under their heads, street lamps and electrical poles with their lights, terminals; 3,522 entity mesh components, 65,204 instances, 2,070 lights, 932 MB |
| `cp_kabuki-18/zz_cp_kabuki.pk3` | gameplay map the owner plays (2026-10-29 15:00): `kabuki-14.glb` (stair/step/ramp props as their mesh, `--col-name-tris` 400: 340 props, 360 k collision triangles) through gltf2map with sloped faces as slabs; 2,071 s to build |
| `cp_kabuki-17/zz_cp_kabuki.pk3` | gameplay map (2026-10-29): map-16's recipe on `kabuki-13.glb`, which adds the devices (the poles collide) |
| `cp_kabuki-16/zz_cp_kabuki.pk3` | gameplay map the owner plays (2026-10-29): `kabuki-12.glb`, collision snapped to 0.1 m (was 0.6), the terrain cells, props as boxes; 310 k triangles + 23 k boxes, 1,799 s to compile |
| `cp_kabuki_scene-20/zz_cp_kabuki_scene.pk3` | render scene the owner plays (2026-10-29): scene-19 plus 42 embedded mlsetups (the shrine gate and 39 more materials no longer grey) and 9 terrain cells; 909 MB |
| `cp_kabuki-13/zz_cp_kabuki.pk3` | gameplay map: spawns under open sky, generated haze sky in its colours before the grade at exposure 1.5 (map 11 with the six sky images swapped) |
| `cp_kabuki_scene-19/zz_cp_kabuki_scene.pk3` | render scene, format version 3, a full rebuild (2026-10-28): the entities' meshes (3,224 components: doors, lamp fixtures, traffic heads, vending machines, signs), their appearances' lights (traffic heads red), the palm, tiled detail (`.xdet`, 2,652 materials); 64,941 instances, 4,441 materials, 2,063 lights, 878 MB |
| `cp_kabuki_scene-18/zz_cp_kabuki_scene.pk3` | render scene, format version 3: scene-17 (a full rebuild: the colour grade, `useInFog`, wide cones, haze volumes (`.xfog`), masked glows, ads per placement and their lights' colours (`ad_lights.json` beside it), neon EmissiveEV) relit with the shadow flags |

**GPU cost:** about 5–6 ms per frame at 1600x900, and 7.6 ms at 4K, measured before the 2026-10-27 look work (the
light in the haze onward). Each feature since, measured on its own at 1080p in hidden look runs, back to back
(absolute frame times differ between sessions, and drifted about 0.5 ms within one evening; on clean camera A the
whole frame now runs about 9.2–9.5 ms at 1080p):

| Feature | Cost | Runs |
|---|---|---|
| Haze (the plain one) | nothing measurable | |
| Light in the haze | about 0.15 ms | 8.18 and 8.30 ms off, 8.42 and 8.36 on |
| Reflections | about 0.25 ms | 6.56 and 6.58 ms off, 6.82 and 6.82 on |
| Wet surface | nothing measurable | |
| Window rooms | nothing measurable | 6.38 and 6.39 ms with flat windows, 6.40 and 6.41 with rooms |
| Colour grade | about 0.02 ms (the post 1x pass, 0.020 ms off, 0.042 and 0.043 on); the frame: not measurable | 6.34 ms off, 6.31 and 6.34 on (one 7.66 ms run off, in the shadow pass) |
| Haze volumes (41 boxes, 11 steam columns) | about 0.2 ms, mostly in the scene pass | 8.52 and 8.65 ms off, 8.75 and 8.82 on (`fogvt-*`, clean camera A) |
| Lamp shadows (one comparison) | about 0.1 ms; the atlas 144 MB, built in 0.9 s at load | scene pass 4.29 and 4.35 ms without (scene-17), 4.48 and 4.38 with (scene-18) (`lstime1-*`); four comparisons: about 0.4 ms (`lstime-*`) |
| A float target (`r_hdr 2`) | about 0.1 ms | frame 8.60 and 8.60 ms at `r_hdr 0`, 8.77 and 8.63 at 2 (`hdrtime-*`, clean camera A) |

The passes, as measured before:

| Pass | Cost |
|---|---|
| Moon shadows | 2.2 ms (the far cascade alone: 1.2 ms) |
| Scene shading | 1.8 ms |
| Depth prepass | 0.7 ms |

## Rebuild and look

```
python tools/levelport/cp2gltf.py scan                          # sectors -> placements.json (~20 s)
python tools/levelport/cp2gltf.py fetch --ents                  # entity templates for lamp and sign lights (~20 s)
python tools/levelport/cp2gltf.py scene --map cp_kabuki --tag N # render scene (~25 min cold, minutes with bakes cached)
python tools/levelport/cp2gltf.py scene --map cp_kabuki --tag N --relight <scene pk3>   # light table only
python tools/levelport/cp2gltf.py scene --map cp_kabuki --tag N --regrade <scene pk3>   # the colour grade only
python tools/levelport/gltf2map.py .codex-research/levelport/cp77/kabuki/kabuki-8.glb cp_kabuki \
    --out .codex-research/levelport/build/cp_kabuki-N --sky cp_kabuki_haze --sky-haze "66 43 40 61 37 36" \
    --sun "0.55 0.65 1 80 200 40" --skylight 150 --worldspawn _ambient=20 --light fast --vis none
python tools/levelport/look.py <map pk3> cp_kabuki --extra-pk3 <scene pk3> --viewpos "-1944 1731 440 315 3" \
    --renderer ../../../.codex-research/renderer-bench/builds/xenjk-vk-dev/RelWithDebInfo/rd-vulkan
python tools/levelport/look_stats.py <ours.jpg> <reference.jpg>
```

- **Long builds.** Launch the full scene build detached (PowerShell `Start-Process` of a bash script). A background
  shell job dies at 10 minutes.
- **Shaders.** Rebuild them with `python tools/scene_spirv.py`.
- **The renderer.** Build it with `cmake --build .codex-research/renderer-bench/builds/xenjk-vk-dev --config RelWithDebInfo --target rd-vulkan_x86`.

## References and the fresh compare

The images and their sources are in `.codex-research/levelport/cp77/kabuki/refs/` (`SOURCES.md`, beside our matching
shots `ours_A.jpg` and `ours_B.jpg`, from the clean camera with the lamps' shadows (scene-18); `ours_*_refpass.jpg`
after the reference A pass (scene-17); before it `ours_A_clean_before.jpg`; and, from the old player camera, `ours_*_grade.jpg` (graded), `ours_*_windows.jpg` (before the grade), `ours_*_wet.jpg`,
`ours_*_hazelight.jpg` and `ours_*_scene8.jpg`). Two references are matched:

- **A:** CD Projekt RED lighting artist Dave Yablonsky's `lighting-kabuki-07`, up the stairs to the pagoda.
- **B:** a night ambience video still, the rooftop overview.

| Shot | Median | Mean | Large-scale contrast | Fine contrast | Top 35% | Bottom 40% |
|---|---|---|---|---|---|---|
| A, ours before the light in the haze | 38 | 48 | 20.5 | 7.4 | 43 | 57 |
| A, ours with light in the haze | 51 | 66 | 39.3 | 6.3 | 87 | 56 |
| A, ours wet | 48 | 64 | 39.5 | 6.4 | 87 | 50 |
| A, ours with window rooms | 50 | 66 | 39.2 | 6.5 | 87 | 55 |
| A, ours graded | 39 | 61 | 48.8 | 6.8 | 90 | 42 |
| A, clean camera, before the reference A pass | 38.5 | 61.8 | 50.3 | 6.5 | 92.7 | 36.7 |
| A, clean camera, after it | 39.4 | 58.5 | 40.4 | 6.8 | 84.0 | 39.9 |
| A, with the lamps' shadows | 38.6 | 57.6 | 40.5 | 6.6 | 83.9 | 38.0 |
| A, linear-light grade, `r_hdr 2` (now) | 39.1 | 58.5 | 40.5 | 6.7 | 84.2 | 38.9 |
| A, reference | 44 | 60 | 39.6 | 11.1 | 84 | 42 |
| B, ours before | 37 | 39 | 16.7 | 6.8 | 38 | 42 |
| B, ours with light in the haze | 37 | 48 | 31.6 | 6.7 | 45 | 41 |
| B, ours wet | 37 | 47 | 31.9 | 6.0 | 45 | 38 |
| B, ours with window rooms | 37 | 47 | 32.1 | 6.4 | 45 | 39 |
| B, ours graded | 36 | 46 | 37.0 | 6.8 | 46 | 35 |
| B, clean camera, after the reference A pass | 35.3 | 46.9 | 30.2 | 7.1 | 45.0 | 41.6 |
| B, with the lamps' shadows | 35.0 | 46.6 | 30.2 | 7.0 | 45.0 | 40.7 |
| B, linear-light grade, `r_hdr 2` (now) | 35.0 | 47.6 | 30.5 | 7.1 | 45.6 | 41.8 |
| B, reference | 60 | 82 | 48.7 | 9.3 | 113 | 55 |

- A's reference is measured with `--crop-bottom 0.06`, which drops its caption strip. "Top 35%" and "bottom 40%"
  are bands of the frame (its top and bottom rows), not the brightest and darkest pixels.
- **Clean camera (2026-10-27).** The rows before it were third-person with the player's body and lit saber in the
  frame (a saber forces third person; its blade lit the wall and stairs blue). `look.py` now looks as a spectator,
  and shot A sits higher and to the right of the ramp, tilted up 3 degrees, so the wall, ramp, pagoda and whole ring
  show. The reference's own camera seems to stand farther back, inside what is a building in our conversion.
- Our A shots before the grade vary a little run to run: the spawn shield's green bubble shows in some of them,
  over the lower middle of the frame. That moved A's bottom 40% by a few points, as between "wet" and "window
  rooms". `look.py` now turns the shield off (`g_spawnInvulnerability 0`), and repeated runs match.
- B's haze is blue-teal (other weather); A's is warm brown, the target the owner chose. Our sky in A's upper left:
  (56, 37, 22) against the reference's (54, 36, 22).
- **What already matches:** the layout; the ring with its ads, struts and a hot spot top right; the pagoda's neon
  trim; the warm haze; the frame's mean, top band and large-scale contrast (40.5 against 39.6);
  `r_sceneFogLight` 1.6 and `r_sceneGradeExposure` 1.5 are calibrated on shot A.
- **Wet ground:** the plaza and roads now mirror billboards, the pagoda, lamps and neon. Without reflections the
  wet ground measured 42.6 in A's bottom 40% (reference 41.7); the reflections lift it to 50 at strength 3.
- **Windows:** the towers are dark with sparse warm lit rooms, as in both references.
- **Colour:** graded, A's bands sit near the reference's: red/green top 1.11 against 1.07, middle 1.13 against
  1.24, bottom 0.76 against 0.95; the darkest 1% 17 against 12.
- **What still differs:**
  - Fine contrast (6.6 against 11.1) and the middle band's warmth: the reference holds more small bright details
    (signs, kiosks, lit doorways, people and cars), orange-lit pipes and structures on its left, and brighter shop
    fronts. Lamp shadows removed leaked light rather than adding edges.
  - The near-black share: 3% of our frame under 20/255, 11% of the reference's (its dark foreground parapet, and
    less haze in front of near surfaces).
  - The pagoda trim's neon reads magenta where the reference's cores are near-white with a red halo (item 8 in
    "Next").
  - The walkway wall's teal is the game's own lamp (`kab_spotlight_b_wall_green_037`, an 8,000 lm instance light
    reaching 5 m); from the clean camera it sits low in the frame.
  - B's sky stays darker and warm, but that reference has other weather.

## Next, in order (owner's pick each step)

1. ~~Light in the haze~~ (done): see "Light in the haze" below.
2. ~~Wet ground~~ (done): see "Wet ground" below.
3. ~~Windows~~ (done): see "Windows" below.
4. ~~Colour cast~~ (done): see "Colour grade" below.
5. ~~Reference A pass~~ (done): see "Reference A pass" below. It took the masked emissives, stand-in ads
   per placement, ad light colours and steam plumes off this list.
6. ~~Lamp shadows~~ (done): see "Lamp shadows" below.
7. ~~A rain mask~~ (done, uncommitted): see "Remaining features (2026-10-28)" below.
8. ~~A linear-light grade~~ (done, uncommitted): see "Linear-light grade" below.
9. **Smaller items** (2026-10-28: all but people and cars done, uncommitted; see "Remaining features"):
   - ~~Traffic-light lights~~, ~~the two missing meshes~~ (the palm; the debug cube stays out), ~~the moon in the
     haze~~, ~~a second main view~~, ~~reflections in motion and under decks~~, ~~reflections without MSAA~~.
   - People and cars: spawned by the game at run time; nothing in the static data places them. Held by the owner.
   - Performance, next by the owner's order: caching the far shadow cascade's static casters (about 1.2 ms of the
     moon's 2.3–2.6 ms), and packing the light shadow atlas tighter (1,533 faces in 9 layers of 2048², 144 MB).
   - New: the colour grade still needs MSAA (it runs in the post 1x pass); without MSAA the frame is ungraded.

## Performance (2026-10-28)

Lap timedemo (`look/timedemo.py --gpu`, scene-19, 1080p, back to back, two runs each; `look/perf5-*`):

| | Frame (median / p95) | Shadow pass | fps (plain) |
|---|---|---|---|
| far cascade drawn every frame | 7.37 / 8.72 ms | 2.63 ms | 122 |
| cached (`r_sunShadowFarCache 1`, the default) | 6.12 / 7.70 ms | 1.36 ms | 139 (4K: 80.5, was 75) |

- **Where it went.** Without the far cascade's scene casters (`r_sceneShadowCascades 2`) the frame was 5.96 ms:
  the cache keeps almost all of that and the far shadows. Rebuild frames (the eye leaving its 12 m cell, about
  every 1.6 s at run speed) cost what a frame did before.
- **Checked.** Moon shadows match with the cache off and on (`look/farcache-sheet.jpg`, debug 4); a lap video on
  scene-19 shows no stale or broken shadows (`look/runvid-f`). Frame-to-frame flicker at a cell change was not
  measured.
- **Left, by size:** scene shading 2.28 ms; the near cascades 1.36 ms (the second about 0.66, empty indirect draws
  about 0.44); the depth prepass 0.71 ms; the light shadow atlas's memory (160 MB with the rain layer).

### 240 fps at 4K (owner's goal, 2026-10-28; committed `d30ea8f3`, `312e5bb2`)

Lossless steps so far, each pixel-identical on fixed demo frames (`look/timedemo.py --shots`): GPU-driven draws
(`r_sceneGpuDriven`), light clusters for surfaces and the haze (`r_sceneClusters`), near-cascade receiver culling
(`r_sceneShadowReceivers`), the SSR depth packed without a copy, the lit pass LESS_OR_EQUAL without debug views,
depth lists biggest mesh first, the reflections' trace on the compute queue (`r_sceneAsync`, -0.13 ms) and the grade
in place (`r_sceneGradeCompute`, -0.11 ms). 4K lap, interleaved A/B: about 11.5 -> 9.3 ms against the first baseline
(10.1-10.4 ms at the machine's current, slower state). The visibility buffer was built, measured 1.3 ms slower and
removed. A vertex-cache reorder of the converted meshes (`scene --vcache`, opt-in) measured no gain: the source
order is already as good. Details, tools and traps: `.codex-research/levelport/cp77/kabuki/NOTES.md`.

After the playtest (2026-10-29, uncommitted): the cheap bounds on Codex Astra's list, in one interleaved run
(`look/p240-ab-sens`, `ab.py --side`): fog volumes off -0.27 ms (the haze pass 0.39 -> 0.13), puddles off -0.02
(the noise cache is not worth building), the haze off -0.47 (the async-haze ceiling). So the haze's local volumes
are now culled per tile (`scene_fog.comp`, see levelport-scene.md "Haze volumes"): the haze pass 0.39 -> 0.14 ms, frame
-0.27 ms (9.01 -> 8.74), fixed frames 90-98 dB with no pixel off (as two runs of one build; `look/p240-ab-fogcull`,
`p240-ds-fogcull-*`). Left on the list: the haze on the async queue (at most ~0.15 ms now), occlusion culling (its
bound: cull against last frame's depth, a lossy test build; worth building only above ~0.4 ms), and from a second
review the AO and glow blends folded into the grade (0.15-0.25 ms, rounding-level). Skipped: skipping the far
cascade's copy (pays only with no moving casters; the player is always one).

## Owner playtest (2026-10-29; uncommitted)

The owner played through `Play_Kabuki.bat` and reported, with screenshots: the saber's reflection beady and far
away, no saber light on the floor up close, walking collision not matching the visuals; then 42-28 a floating traffic
light, 42-34 a hole in the ground, 43-10 ads under the walkway, 43-34 the shrine untextured.

- **Saber light.** The scene took none of the game's dynamic lights. Now the eye view's positive dlights (up to 32)
  are lit like the source's lights (`scene.frag`, after the light loop; same falloff and BRDF). JA sizes them for
  lightmapped maps (a saber about 45 u, reaching no floor), so `r_sceneDlightReach` 2.5 multiplies the radius and
  `r_sceneDlights` 150 sets their power (dark wet asphalt needed it). Not timed; Astra puts it at up to ~0.2 ms in
  effect-heavy views.
- **Beads.** The reflection history gets four more colour levels (blitted after the capture), and a hit reads the
  level its glossy cone covers (`scene_ssr.comp`), so a blade mirrors as a streak; puddles stay sharp
  (`look/bead-far` against `look/bead-far-mips`).
- **Grey shrine.** `wbr_jpn_gate_a` and 39 other materials keep their mlsetups inside the `.mesh` (exported as
  `<mesh>.mesh_embedded\<FNV-1a64 of the original path>`). `fetch --embedded-ml` writes them out (42).
- **Hole.** The scan skipped `worldTerrainMeshNode` (its mesh is in `meshRef`): 9 terrain cells now come in, in both
  the scene and the collision. Not checked at the owner's spot.
- **Collision.** The 0.6 m vertex snap moved floors by up to 11 u; `--col-cell` is now 0.1 (about 4 u). Real prop
  shapes (railings, stairs, benches) were tried: `kabuki-9` (finest LOD, 1.81 M triangles), `kabuki-10/11` (coarsest
  LOD, props by fill or name, 0.64/0.71 M) never finished compiling (`q3map2 -bsp -meta` is single-threaded and
  slow with one thin brush per triangle; maps 14 and 15 stopped after 40+ CPU-minutes). `kabuki-12` keeps props as
  boxes: map-16. The `--col-prop-*` options stay for a cheaper prop representation.
- **Traffic lights** (owner: fix). The poles were never missing from the data: they are devices
  (`worldDeviceNode`, `street_sign_d_dst.ent`), which the scan skipped, and their mesh is a destruction component,
  which the entity reader skipped. Both read now: scene-21 and map-17. Street lamps, electrical poles and terminals came
  in the same way (their lights too: rows 3-4 of `look/poles-20-vs-21-centre.jpg`, scene-20 left, scene-21 right).
- **Walkway ads:** left as the game places them (owner, 2026-10-29).
- **Collision against the scene** (owner 2026-10-29: stairs that collide like boxes; "can we do some compare of the
  collision map geometry overlaid the actual"). `.codex-research/levelport/look/kabuki_collide.py` <map pk3> <scene pk3>
  <out> [viewpos...]: per 16-u column the BSP's player-solid brushes (sky shell left out) against the scene's LOD-0
  walkable surfaces below 600 u (col_height): matched, buried (inside collision), missing (none), invisible tops; side
  slices through given points; `stats.json`, `panel_*.png`. Map-17: matched 626,211, buried 36,946, missing 19,171,
  invisible tops 66,497 (`look/collide-20261029b`).
  - **Cause** (agent census, `look/stairs-agent-20261029/`): gltf2map `mesh_brushes` extruded every sloped triangle
    vertically to its own far vertex, so a stair flight's sloped underside filled the steps above it and sloped roofs
    were solid wedges; and with `--col-prop-tris 0` stair props under 6 m were bounding boxes (385 placements), while
    `COL_MESH_NAMES` searched the whole depot path (`arch` in `\architecture\`).
  - **Fix:** sloped faces are slabs (vertical sides, a cap parallel to the triangle, `thickness` deep vertically);
    props whose file name says stair/step/ramp collide as their mesh up to `--col-name-tris` (400) whatever
    `--col-prop-tris` says; names are matched on the file name. All of COL_MESH_NAMES added 109 k triangles (1,898
    props), too many for q3map2, so the walked names only (+18 k).
  - **Map-18:** matched 630,176, buried 32,540 (3,458 cells cleared, 13 new), missing 19,612 (549 new cells: cracks
    the wedges hid), invisible tops 57,748 (`look/collide-20261029-map18`, `stairs_spot2_17_vs_18.png`). Walk test
    (`look/stairs_walk.py`): at the owner's spot 2 a player runs up both flights (eye z 113 -> 301) on map-18 and
    stays at street level against the block on map-17.
  - **Compile time** (owner: fine now, "we should work for a future where that is shorter"; then "do it here"):
    map-18 took 2,071 s, 1,674 of them q3map2 `-bsp` on ~355 k one-triangle detail brushes. That stage grows faster
    than the brush count (`look/compile_scaling.py`: 47 k brushes 19 s, 94 k 113 s, 378 k 1,674 s; q3map2 hashes
    planes only by `(int)|dist| & 8191`, so lookups slow as distinct planes pile up). gltf2map now merges coplanar
    edge-neighbour triangles of a node into convex slabs (`merge_coplanar`, up to `--merge-verts` 12 corners; the same
    vertical sides and parallel cap, so the same solid), refuses near-straight corners (q3map2 called their two side
    planes one, "duplicate plane", and dropped a side: 1,759 in map-19) and splits any slab that still has such a
    pair back into triangles; and runs the nodes in `--jobs` processes (the Python step 311 -> 90 s, the .map
    byte-identical). **Map-20**: 207,705 mesh brushes, `-bsp` 639 s, total **774 s** (12.9 min, was 34.5).
  - **Same collision:** per column, map-20's solid (brushes merged into intervals) against map-18's: of 312,879
    columns, 13 differ in interval count and 7 by more than 0.5 u. `merge_check.py` found no point inside one brush
    set and outside the other on 10 nodes (400 k points). kabuki_collide now merges a column's brushes before
    classifying (a top inside another brush is no floor: stacked one-triangle slabs had counted buried tops as
    matches, so the earlier counts above undercount buried surfaces); on that metric map-18 and map-20 are matched
    510,318 / 510,318, buried 147,044 / 147,040, missing 24,966 / 24,970. On it the stairs fix reads map-17 -> 18:
    matched 486,953 -> 510,318, buried 171,588 -> 147,044 (-14%), missing 23,787 -> 24,966, invisible 62,880 ->
    53,961 (`look/collide-u-map17/18/20`). The owner keeps playing map-18 (the same collision); later builds use the
    new path.
- **Cost** (4K lap, interleaved A/B against `d30ea8f3`, 4 rounds each; `look/p240-ab-fixes-*`): the dynamic lights and
  reflection mips cost nothing measurable, with the saber holstered (frame +0.00 ms, the mips' blits +0.035) or lit
  (`runvid-d`'s lap: +0.00). Fixed frames differ only where intended: the wet ground's reflections, and the lit
  blade's light on the ground (`look/p240-fixes-sheet-*.jpg`).

## Owner playtest 2 (2026-10-29 evening, `/screenpos` reports; triage only, nothing fixed)

The owner played map-18 / scene-21 with the Kabuki play DLL and filed 11 `/screenpos` reports
(`GameData/base/screenshots/screenpos/index.txt`, 21:57-22:04) plus six plain shots (21:56-21:57) of a character
afterimage. Tools: `look/afterimage/` (`spin_check.py`, `scene_ray.py`, `pix_ray.py`, `spawn_check.py`) and
`look/collide-owner-20261029/` (kabuki_collide with the reports' positions; `owner_spots_zoom.png`).

- **Afterimage when turning** (reproduced): a dark copy of her head and hair beside her while the view turns. Spin
  test at the owner's 4K settings, open street: 3 of 4 mid-turn frames show it as played, 0 of 4 with
  `r_sceneReflect 0`, 0 of 4 with `r_ssao 0` (which also turns the reflections off: they need the depth extract,
  `vk_init.cpp:584`). Cause: the wet ground's screen-space reflections trace through last frame's image, characters
  included; street next to her head takes her hair as a hit, and the settle keeps 85% of the previous result, so the
  copy trails as the world turns. **Fixed** (uncommitted): `scene_capture.comp` keeps the depth extract's character
  mark (stencil `VK_STENCIL_AO_MASK`, negative depth) in the history; `scene_ssr.comp` neither hits nor mirrors from
  such texels (rays pass through characters; normals still read their depth). Check (`spin_check.py`, 4K, same
  spot): ghost in 3 of 4 mid-turn frames before (`b_base`), 0 of 4 after (`c_base`, `afterimage/fix_heads.jpg`).
- **Flickering sign** (-1328 -2064 98): mesh 3722 draws an emissive poster layer (kind 3) at exactly the depth of its
  opaque poster (`scene_ray.py`: both at 242.87 u); the lit pass's EQUAL test against the prepass kept whichever
  triangulation came out nearer per pixel. **Fixed** (uncommitted): `scene.vert` draws emissive materials 0.1 u +
  0.1% of the distance nearer along the eye ray in every pass (prepass and lit alike; the draw layout's binding 15,
  the materials, now reaches the vertex stage; lamp-shadow views push their own origin as the eye). Check: the
  owner's view pinned with `benchcam`, nudged 0.5 u five times: before, black shards changing shot to shot; after,
  the clean poster in all five (`afterimage/sign_ab.jpg`).
- Candidate renderer: `look/afterimage/dll-fix/rd-vulkan_x86.dll` (6cdafbe6, sha256 prefix): the kabuki-play DLL's
  tree (77d56d8c + gore fix) plus these two fixes; not yet swapped into `.codex-research/kabuki-play/`.
- **Hole in building** (block around -500..-600, -2400..-2900): rays through the dark area behind the wall-mounted
  AC units hit nothing for 1,858 u; looking up, the black panels are the undersides of bare floor plates (mesh 131).
  The block's facade meshes are missing from the scene (not yet traced to the fetch).
- **Can't walk forward** (-1373 -1972 -145, alley): a solid block from ~25 to ~65 u above the floor, ~115 u long, whose
  only drawn surface is its top: a thin overhead object (hanging sign or awning) collides as a block down to head
  height.
- **Standing on nothing** (-1426 -2173, -537 -1224, alleys): she stands 8-10 u above the drawn floor; low invisible
  lumps on the alley floor (litter meshes colliding).
- **Roof hard to walk** (-639 354 298): the tiled roof's collision follows each tile ridge, bumps of a few units every
  ~20 u along a 34-degree slope.
- **Spawn in wall** (-615 -2391 41): `spawn_check.py`: 7 of 16 `info_player_deathmatch` boxes overlap player
  collision (this one inside a thin wall); spawn points are not checked against the mesh collision.
- **Not investigated yet:** the free-standing poster (-1818 -2505, a single emissive plane; maybe a hologram in the
  source), pedestrian signals with no WALK / DON'T WALK state, low-resolution window interiors.

### The rest of the playtest (owner: "what about the rest of the issues", then "make this work", cycling signals with
the game's sounds; committed 2026-10-30: 6eac80aa gore fix, 9c7146d5 renderer, 02f92347 cgame, 3b14e1c2 converter;
installed as Install 47 (map-22, scene-23, the sound pack) and Install 49 (the renderer and cgame in GameData, which
`Play_Kabuki.bat` now uses), see renderer-vulkan-in-xenjk.md)

Two agents traced the scene reports (`look/walls-agent-20261029/`, `look/visuals-agent-20261029/`), a third the
game's signal system and its sounds (`look/signals-agent-20261029/`).

- **Collision, by file name** (cp2gltf `COL_NONE_NAMES`, `COL_THIN_NAMES`, `COL_HULL_NAMES`, `--col-names 1`):
  grass, weeds and litter (bottles, cans, cigarettes, papers, street/cardboard trash; 1,267 placements over 1 m) no
  collision; awnings, canopies, umbrellas, tarps their own coarsest mesh up to `--col-thin-tris` 300 (116), else
  none (29: an awning's box hung to head height, `poor_awning_d` at the "can't walk forward" alley); the pagoda's
  traditional roof pieces (`wat_kab_building_f_roof*`, 352) their convex hull (the tile ridges were steeper than a
  walkable slope; other roof families have parapets a hull would fill). Spawns also need side clearance
  (`clearance()`, 0.75 m). Map-21 (`names-21.sh`, 744 s): spawns in collision 7 -> 1 (one corner; 0.75 m clearance
  since), whole map matched 510,318 -> 513,439, buried 147,044 -> 137,390, missing 24,966 -> 31,499 (the litter);
  slices at the report spots `look/collide-owner-map21/compare_18_21.png`: the awning is a thin slab now, the roof
  smooth (about 9 u over the tiles at its ridge line, and its eave's underside filled); "standing on nothing 1"
  keeps a lump at her feet from a mesh not yet named. Map-22 (`fixes-22.sh`, `kabuki-16.glb`, 1,116 s, against
  scene-22 with the new walls): spawns in collision 0 of 16; matched 514,713, buried 139,019, missing 31,601
  (`look/collide-owner-map22/`).
- **Hole in building:** `.w2mesh` placements (the older mesh format: this block's facade bays, balconies, roof pieces,
  ~290 cables; 365 placements, 30 meshes) were dropped by `load_placements` (`.mesh` only). `MESH_EXTS`,
  `mesh_stem()`, and the uncook renames `<stem>.w2mesh.glb`. Scene-22 shows the walls.
- **The free-standing poster** was the stall's food-ad monitor: a 4.6 m billboard template the instance scales to 0.16
  (`instanceData`, matched through its `CruidDict` to the template's component id). `instance_mesh_overrides()` keeps
  visualScale / isEnabled / chunkMask / meshAppearance / mesh per instance (78 entities); scene-22 shows the monitor.
- **Window interiors:** the source's room atlases are 1024 x 256 (ours not downscaled; BC1, 37 dB against the source):
  left as the source's.
- **Signals cycle as the game's** (scene-23, renderer and cgame below):
  - The traffic system drives them: each placed signal's `worldTrafficLightListenerComponent` names its intersection
    and light group; the `compiledTraffic_Intersection_*` nodes hold each group's program (colour, seconds), every
    group of an intersection on one clock (e.g. cars green 16.83, yellow 2.5, red 23.33; crossings walk 10, flashing
    8.33). A crossing lights its walking figure on green, blinks its hand on yellow (the `blink_01` curve, 1 s) and
    holds it on red. `signal_groups.json` (the agent's `build_groups.py`) is copied into the work folder.
  - Converter: each signal head's face splits by its vertex colour into the lamps' zones (which lamp lights which
    zone: the entity's light components' `materialZone`), each zone its own glowing material (flags 8 | lamp << 4);
    the instance carries its signal slot; every lamp's light is kept, tagged (type bit 3, lamp, slot);
    `maps/cp_kabuki.signals` lists each slot's cycle, offset and position. Scene-23: 61 heads, 145 lamp lights.
  - Renderer: `Scene_SignalStates` gives each slot's red / yellow / green each frame from the game clock
    (`tr.refdef.time`) into the frame block; `scene.frag` scales a lamp material's glow and a lamp light by it.
    `r_sceneSignals` (1; 0 every lamp lit), `r_sceneSignalShift`, `r_sceneSignalAt` (cheats, checks).
  - Sounds (`codemp/cgame/cg_signals.c`): the crossings' Wwise events from `sfx_container.bnk`, rebuilt as the game
    schedules them: walk = the tick bed every 3.0 s (-13 dB) plus a chirp every 3.5 s from 1.5 s in, ten times;
    don't walk = the tone every 3.5 s plus its chirp likewise; positional, within 40 m; same clock as the lamps.
    `zz_cp_kabuki_sfx.pk3` (`look/afterimage/signal_sounds.py`, game audio, local only).
  - Checked (`spin_check.py phases`, `r_sceneSignalAt`): a car head green 5 s / yellow 17.2 s / red 30 s, a crossing
    hand then walking figure, the hand lit at 32.70 s, dark at 33.20 s, lit at 33.45 s (`look/afterimage/
    s23_phases.jpg`, `s23_blink.jpg`); with `s_show 1` the corner's six crossing heads played 64 sound starts in 25 s
    at the 3.0 / 3.5 s rates.
  - Not carried: the one crossing in a "glitch" ambient area (its random glitch layers), cycle start times (the
    engine's: a fixed offset per intersection), the Wwise attenuation curve (the engine's default falloff).

## Remaining features (2026-10-28)

The owner asked for the rest of the features, the small ones included, people and cars left out. All uncommitted;
the renderer is the dev DLL. Design: [levelport-scene.md](levelport-scene.md).

- **Rain mask** (`r_sceneRain`). A top-down view of the scene at load, one more layer of the light shadow atlas
  (306 m across, 15 cm a texel, 16 MB, 0.2 s): floors under the walkway deck, the overpass, canopies and indoors
  stay dry (`look/rain-*`, debug 9). Four comparisons left stair steps on the drip lines; a 3x3 tent smooths them.
- **The moon in the haze** (`r_sceneFogMoon`). The volume is lit after the cascades (`vk_scene_haze`) and reads
  them: the moon's light scatters in the haze, dark under decks and buildings. It lifts the frame a little: shot A's
  mean 52.4 to 55.8, ground views +2 to +5 (`look/moon-sheet.jpg`). Cost: not measurable.
- **Each view its own eye.** Draws push their view's origin; only the view the haze volume and reflections were made
  for reads them (a portal or mirror view used the first view's). Found on the way: after the haze-over-sky
  triangle the draw zeroed the whole push block, view matrix included, so glass and decals drawn after it in the
  main view had no matrix while the haze light was on. Fixed.
- **Reflections.** Settling weighted by brightness (Karis): a lit saber's reflection was blocky white sparkle, now a
  soft streak (`look/runvid-d/saber-sheet.jpg`, left before). A texel with no history takes two rays (the motion
  blotches). A rising ray that meets a deck's visible top passed under it and mirrors the dark underside. Without
  MSAA the capture runs at the end of the single-sample pass: the wet ground mirrors as with MSAA
  (`look/nomsaa-sheet.jpg`). Reflections cost 0.25 ms in all (6.83 ms off, 7.08 on, scene-18).
- **Tiled detail** (`r_sceneDetail`, `--detail-size 512`). The bakes no longer alias the main layer's texture: the
  near asphalt shows its grain, cracks and tyre marks (`look/s19-near-crop.jpg`: scene-18, scene-19 with detail
  off, on). 851 composites re-baked on 12 processes (`--bake-workers`; in the scene stage one at a time took about
  8 s each). With detail off the main layer shows its mean, flatter than before.
- **Entities.** The traffic heads were not in the scene, nor were 3,200 other entity mesh components (doors, lamp
  fixtures, vending machines, TVs, signs, crosswalk stripes): `fetch --ents` now reads the appearances and places
  them (tools/levelport/README.md, "Entities"). 61 traffic heads light their red lamp; 30 `emissiveOnly` indicator
  lights are left out. The palm came in with a rerun of `fetch --lods`; the debug cube (`user\` folder, not in the
  shipped archives) is skipped. One template is missing from the archives.
- **Cost** (lap timedemo, 1080p, dev renderer, back to back): scene-18 126–127 fps, GPU median 7.05–7.11 ms; scene-19
  122–123 fps, 7.33 ms (the cascades 2.64 ms, shading 2.28); 4K scene-19 75 fps. Scene-18 at 6.55 ms earlier the same
  evening ran the same fps; the 0.5 ms is in no pass, and the moon (7.05 off) and reflections do not account for it.
- **Not done:** the colour grade still needs MSAA (it runs in the post 1x pass), so without MSAA the frame is ungraded.

## Linear-light grade (2026-10-28)

The grade used to undo our tone curve from an 8-bit frame, so neon cores reached it already clipped, and the haze
was mixed over the finished colour. Now:

- **Headroom.** scene.frag's encoding carries light past white: the tone curve up to 0.999, then an e-fold of light
  per 0.1. On an 8-bit target that clips where it did before. The new `r_hdr 2` (a 16-bit float target, latched) keeps
  it, so the grade, which decodes the same curve, gets the light that was drawn. Screenshots and video capture read the
  float target.
- **Haze in linear light.** The haze colour and the light scattered in it are mixed into the surface's linear light,
  then tone mapped once (they were mixed over the display colour).
- **Glow keeps its hue.** The glow extract compressed each channel on its own: a red neon's blue caught up with its
  saturated red and the halo went pink. It now compresses on the brightest channel.
- **SSR.** `scene_ssr.comp` spreads wet rays at roughness 0.2, as scene.frag caps it (it used 0.12).
- **Debug 8 reads emissives too**, and `look.py --png` with `--cvar r_ext_multisample=0` gives exact material ids
  (round to the nearest step: the float capture can be one low).

**The pagoda trim.** It is `emissive_kab_red` on `wall_lamp_a` (metal_base: EmissiveColor 255, 50, 111 at EV 7),
converted right. Its pink came from the tone curves, not the material: in linear light it is about (4, 0.11, 0.64),
and per-channel curves saturate red first and let blue climb. With the hue-kept glow and `r_hdr 2`, the trim's red
falloff is (159, 66, 56) against the reference's (162, 56, 46) (was 159, 71, 73; trim crop, `look/lin-a-hue-trim.jpg`).
The cores still pass through pink on their way to white: the reference's are near white (247, 232, 225), ours
(218, 175, 161). Emissives x4 whitened them but blew out the windows (`lin-a-em4`), and glow 8 only widened the halo
(`lin-a-hue8`); both kept at their defaults.

- **Frames.** Shot A: mean 58.5 against 57.6, bottom band 38.9 against 38.0, fine contrast unchanged
  (`lin-a-hue3`). Shot B: mean 47.6 against 46.6 (`lin-b-h0`, `lin-b-h2`; the sheet in `lin-b-sheet.jpg`). `r_hdr 2`
  changes the whole-frame numbers by under 0.2; it shows in the cores.
- **Cost.** `r_hdr 2`: about 0.1 ms (8.60 and 8.60 ms at `r_hdr 0`, 8.77 and 8.63 at 2, alternating, `hdrtime-*`).
- **On by default (owner, 2026-10-28).** A map whose scene has a grade switches `r_hdr 0`'s 8-bit target to float at
  load (`r_sceneHdr 1`), and a map without one switches it back. The scene look runs no longer need `--cvar r_hdr=2`;
  `--cvar r_sceneHdr=0` gives the clipped frame. Checked: Kabuki at the defaults matches the `r_hdr 2` frame
  (`scenehdr-a7`), and Kabuki then `mp/ffa3` switches back and draws the same as without the switch
  (`scenehdr-stock`, `scenehdr-stock0`).

**Sharpness.** Our shots read softer than reference A. It is not the resolution: at the reference's 1920x1080
(`blur-fog`, PNG) our pixel-scale detail relative to 8-pixel detail matches it (0.42 against 0.37), but detail at
every scale is about 60% of the reference's (mean gradient 3.3 against 5.5). Without the haze it is about 80%
(4.4, `blur-nofog2`), so the haze's veil over the near plaza is most of it; the rest is the reference's small bright
details (people, cars, small signs, glints). The usual 1600x900 JPEG shots, scaled to compare, lose a little more
(Laplacian 5.7 against 7.1 at native PNG). Side by side: `look/blur-sheet.jpg`.

Follow-ups (2026-10-28):

- **Haze start.** The source's haze is two layers: a volumetric fog near the eye, which shows mostly where lights
  scatter in it, and a distant fog that starts at 150 m (`24h_weather_pollution`, `DistantFogAreaSettings`
  `nearDistance`). Ours veiled everything from 0 m toward the haze colour. `r_sceneFogStart` (metres; 0 is the
  old look) now keeps the dimming over the whole path but fills in the haze colour only past it. The owner chose
  the source's 150 m as the default (2026-10-28). Shot A, 1080p PNG
  (`fogstart-*`, sheet `fogstart-sheet.jpg`):

  | Start | p01 | p10 | p25 | mean | under 20 | fine | mid | bot40 |
  |---|---|---|---|---|---|---|---|---|
  | reference | 1.4 | 17.6 | 27.1 | 58.1 | 13.7% | 11.4 | 53.6 | 39.2 |
  | 0 m (now) | 18.3 | 26.1 | 33.3 | 58.7 | 1.9% | 6.7 | 54.3 | 39.1 |
  | 75 m | 10.1 | 18.6 | 25.6 | 53.6 | 13.0% | 7.0 | 48.4 | 31.5 |
  | 150 m | 5.6 | 16.3 | 23.4 | 51.9 | 17.3% | 7.3 | 44.5 | 30.5 |

  75 m matches the reference's shadows (p10, p25, the dark share) and reads clearer, but the plaza and middle band
  lose brightness the veil was supplying. Shot B at 150 m goes much darker (mean 47.6 to 39.8; its reference is
  other weather).
- **Surface detail.** Even with no haze the near plaza has 74% of the reference's detail (gradient 4.7 against 6.4
  in the bottom 45%). Multilayered materials (2,439 of 3,919; 410 MB of the 601 MB of images) are baked by
  `cp77mat.py` into one 512² image over the mesh's UVs, so the source's tiled layers (`matTile` x
  `tilingMultiplier`) keep only a few dozen pixels a repeat: the ground and ramp read smooth (`blur-near-crop.jpg`).
  The reference camera also sits closer, so its objects get more pixels.

## Lamp shadows (2026-10-27)

The source's shadow-casting lights (`enableLocalShadows`: 308 of the scene's 2,023 lights) now have static shadow maps,
built at map load: 1,533 faces (a cone face for 63 spots, six cube faces for 245 lights) in 9 layers of 2048² (D32F,
144 MB), in 0.9 s on the RTX 3080. Design in [levelport-scene.md](levelport-scene.md), "Light shadows". Scene-18 is
scene-17 relit with the flags (`--relight`, about 5 minutes).

- **What changed.** Lamps no longer light through walls, eaves and floors: the pagoda's red walls under its eaves,
  the plaza under the walkway roof and the rooms' light spilling into the plaza haze now fall dark
  (`look/ls18-crop.jpg`, `look/lsclose-sheet.jpg`, off left and on right). The haze's light falls in shafts.
- **Shot A.** Mean 58.5 to 57.6, bottom band 39.9 to 38.0; fine contrast 6.8 to 6.6. The shadows take away leaked
  light more than they add edges: the fine-contrast gap to the reference (11.1) is its bright small details, not
  missing shadows.
- **Filtering.** One bilinear comparison: four were indistinguishable at shot A's distance (`look/ls18-taps.jpg`) and
  cost 0.3 ms more.
- **Checks.** No acne or light leaks at the close views (`lsclose-*`); scene-17 (no flags) draws as before with the
  new renderer (`ls-smoke17`, identical statistics).
- **Traps.** A light's own fixture would shadow it: the faces start a quarter metre out, depth clamp is off for
  them (the cascades clamp), and emissive materials (lamp lenses) cast nothing.

## Reference A pass (2026-10-27)

The owner sent reference A and our matching shot to Codex (`gpt-6-astra`, high) for a visual difference analysis
(`.codex-research/levelport/look/codex-visual-A/analysis.md`) and asked for each item to be worked through, merged
with this list. Builds: scene-14 (`--relight`, wide cones), scene-15 (full: masked glows, ads), scene-16
(`--relight`, haze volumes), scene-17 (full, all of it plus neon EmissiveEV); maps 12 and 13 (sky). Looks:
`cam-a*` (camera search), `clean-base`, `ring-*`, `s14-a` to `s17-b`, `fogvt-*` (timing); the sheet
`look/refA-pass-compare.jpg`.

| Item (Codex's rank) | Finding | Done |
|---|---|---|
| 1. Framing | Third person: body and lit saber in view; the wall hid the ramp | spectator camera, `--viewpos` pitch; shot A moved |
| 2. The ring's white disc | All haze light: off, the ring read like the reference. Its seven haze lights are 220-degree areas facing outward, drawn as spheres | wide spreads become cones: pixels over 200 fell from 4.6% to 1.0% (reference 2.2%), the ring's struts and ad show, its hot spot sits top right as in the reference |
| | The halo was green-white | the haze's albedo, the pollution weather's warm (1, 0.925, 0.794) (`r_sceneFogAlbedo`) |
| 3. Warm content | Repeated stand-in ads; 25 masked glows in use not drawn (paper lanterns, banners, lightbox faces) | ads per placement; masked glows; ad lights in their ad's mean colour (170 of them); middle band red/green 1.04 to 1.13 (reference 1.24) |
| 4. Teal wall, sky | The wall's lamp is the source's own (an instance light, 8,000 lm, 5 m); the sky was olive | lamp kept; sky solved through the LUT: (56, 37, 22) against (54, 36, 22) |
| 5. Haze depth | No steam or local fog | the source's 41 fog volumes and 11 steam columns in the haze (`.xfog`, `r_sceneFogVolumes`), about 0.2 ms |
| 6. Fine contrast | Lamps cast no shadows; ambient 20% lower changed nothing measurable (the dark areas are haze, not ambient) | ambient kept; lamp shadows built after the pass (see "Lamp shadows") |
| 7. Wet ground | Wet roughness 0.12 everywhere read as plastic | wet roughness down to 0.2 at most; the rain mask is item 7 |
| 8. Lens and bloom | More glow or a lower threshold didn't change the trim; its colour is the grade's clipped highlights | glow kept; item 8 |

- **Exposure.** With the ring's disc gone the frame fell to a mean of 54. `r_sceneGradeExposure` is now 1.5 (it
  was calibrated at 1.3 with the disc in): mean 58.5 against 60.4, top band 84.0 against 84, bottom 39.9 against
  41.7, large-scale contrast 40.4 against 39.6.
- **Camera noise.** The pitch is held for a number of frames, so repeated shots shift by a pixel or two: compare
  statistics, not pixel diffs.

## Colour grade (2026-10-27)

The owner picked reference A's warm look as the target (B is other weather).

- **Diagnosis.** The materials were not the cause. The walkway bakes to a pale blue, but that is its paint layer's
  own tint (`ml_pedestrian_bridge_b`: `concrete_new_painted` at linear 0.27, 0.37, 0.51). The template colour
  textures average 0.5 linear, so "tint × texture × 2" is right. The lamps' cyan is in the source too. The
  difference was CP's colour grade, and lamps lighting the haze.
- **CP's grade.** The master environment (`cp2077_master_env_ep1_v006.env`) sets `ColorGradingAreaSettings`: a 32³
  float LUT taking scene light in ARRI LogC (v3, EI 800) and giving display light, its tone curve included. The SDR
  LUT is `cp2077_gen_lut_nge_v017.xbm`; the 1.x LUTs (`cleangoodb_v0020q2` to `v0032f`) respond within a few levels.
  The weathers (`24h_weather_pollution.envparam`, ...) leave it alone. It warms the midtones (grey 0.18 shows as
  sRGB 152, 136, 117), turns shadows green-yellow (0.03 as 37, 38, 30), and crushes red in colours that have little.
- **In rd-vulkan.** `vk_scene_grade` runs after the post 1x blends, before bloom and the HUD. It copies the frame and
  sends each pixel back through scene.frag's tone curve, times `r_sceneGradeExposure`, through LogC and the LUT, and
  out sRGB-encoded. Gamma 2.2 there lifted the LUT's black (0.0034) to 19/255; sRGB keeps it at 11, as in the
  reference. It needs MSAA. Debug views stay raw.
- **Lamps out of the haze.** Light components (lamps, spotlights, signs) almost all carry `useInFog 0` (1,040 of
  1,044 in the sectors, every one in the entity templates); static light nodes have no such field. The converter
  gave them their `scaleVolFog` share anyway, so the cyan lamps filled the haze. 202 lights left it (949 → 747).
- **Calibration.**
  - `r_sceneGradeExposure` 1.3: the frame's mean on shot A (1 left it dark, 1.5 matched the median but brightened
    the top third to 94). The reference A pass later set it to 1.5, once the ring's white disc no longer lifted the
    frame.
  - The sky and haze colours, matched to A after the grade, were set before it: solved through the same LUT so that
    each lands where it used to. Sky `--sky-haze "65 52 40 60 46 36"` (was "70 50 28 62 44 24"; now "66 43 40 61 37
    36" for exposure 1.5); `r_sceneFog` 51 38 32 (was 50 36 20). Without the grade (`r_sceneGrade 0`, or no MSAA)
    both read greyer.
- **Cost.** About 0.02 ms per frame at 1080p.
- **Runs:** `.codex-research/levelport/look/cast-*` (debug views and the ungraded baseline), `grade-a`/`grade-b` (the
  first grade), `grade-a2` (sRGB out), `grade13-*` (`useInFog`, exposures), `grade14-*` and `grade-final` (sky and
  haze before the grade), timings in `gradetime-*`, side by side in `grade-compare.jpg`. The extracted environments
  and LUTs are in `.codex-research/levelport/cp77/env/`.

## Windows (2026-10-27)

- **Before:** every window glowed in one flat tint (the converter ignored the material's parameters).
- **Source parameters:** the 332 `window_parallax_interior` records (35 materials in Kabuki's scene) carry:
  - room size, 3–6.5 m by 2–4 m;
  - a dark share: 0, 0.5 or 1 (all dark);
  - EmissiveEV, from 0 to 8;
  - a colour spread;
  - a tint, some red or pink;
  - a RoomAtlas: four (or two wide) pre-rendered rooms side by side.
- **Now** (converter, scene-10, and `windowRoom()` in `scene.frag`): each room picks an atlas room, goes dark at the
  source's share, and lit ones vary in brightness and warmth. The view ray is traced into the room's box and mapped
  onto the picture.
- **Room grid:** mesh space. The first try used world space, and rooms split windows (half a pane lit). A debug
  view of the window UVs showed they repeat per window, so they can't tell rooms apart; mesh space lines the rooms
  up with the frames.
- **Result:** the towers go dark with sparse warm windows, as in both references. Frame brightness barely moves
  (windows are a small share); shot B's fine contrast goes from 6.0 to 6.4.
- **Cost:** none measurable (scene-9 against scene-10, shot B: 6.38 and 6.39 ms against 6.40 and 6.41 ms).
- **Not done:** curtains (`Curtain`, `CurtainMaxCover`), the `LayerAtlas` foreground layer, `positionX`
  randomisation.
- **Runs:** `.codex-research/levelport/look/win-a` (world-space rooms), `win-uv` (the UV check), `win-b` (mesh
  space), timings in `wintime-*`.

## Wet ground (2026-10-27)

- **Surface.** Up-facing surfaces darken (albedo × 0.45) and go glossy (roughness toward 0.12; since the reference
  A pass, down to 0.2 at most). Gradient-noise
  puddles cover 30%: near mirrors, the normal map flattened. Value noise drew square puddles.
- **Highlights.** Each light's specular lobe widens with its size (0.3 m). That alone barely shows lamps in the
  street: a light lights only points inside its radius, and a mirrored lamp shows far beyond it. So reflections
  are screen-space.
- **Reflections.** The frame's resolved image and depth extract are packed at half size after the MSAA resolve; no
  extra render-pass breaks are needed, because both are already there.
  - **First version:** each wet fragment marched its own ray through last frame's history. With no temporal filter,
    hits and misses flickered pixel by pixel, worst under the walkway deck.
  - **Now:** a compute pass rebuilds each texel's surface from depth, traces it with a start and spread that change
    every frame, and settles the result over frames (15% new, reprojected, kept where the depth matches).
    Fragments read it with a depth-checked tent filter.
- **Tuning that mattered:**
  - Rays follow the flat surface (80%) over its normal map; no per-pixel jitter.
  - A 2x2 quad average from fine derivatives.
  - Rays march on past thick occluders.
  - Misses mirror the haze at a fixed partial weight, because telling sky from hidden geometry per pixel drew
    blotches.
  - Strength 3 by eye.
- **Settling:** "same surface" depth checks had to be loose (10% when shading, 8% when settling). At 3–4%, grazing
  ground rejected its neighbours and the reflections stepped in half-resolution blocks. A bilinear read left grain
  in motion; the 1.5-texel tent made it soft.
- **Runs:** `.codex-research/levelport/look/wet-*`, `ssr-a` to `ssr-k` (single frame), `ssrt-a` to `ssrt-c` and
  `ssrt-move` (settled, and a camera sweep), timings in `ssrtime-*`, `wettime-*` and `ssrttime-*`.

## Light in the haze (2026-10-27)

- **How.** A froxel volume over the eye view: 240x135x64, slices exponential from 1 m to `r_sceneFogFar` (600 m). Each
  froxel takes its light-grid cell's lights times their `scaleVolFog` share, with a Henyey-Greenstein phase
  (`r_sceneFogPhase` 0.5). A column pass integrates toward the eye. Surfaces add it at their depth, converted back
  to linear light through the inverse tone curve, so the existing haze looks the same when it is 0. The sky gets
  the far end from a full-view triangle at depth 1. Design and format are in [levelport-scene.md](levelport-scene.md).
- **Strength.** `r_sceneFogLight` 1.6 (0.25, 0.6, 0.8, 1.2 and 1.6 tried; 4 white-washes the frame).
- **Froxel artefacts fixed.** Point sampling a light's peak drew fixtures as bright blocks: the peak is now averaged
  over the froxel (along the ray, the mean of 1/(x²+1) over D is about π/D; across, at least a froxel wide). 160x90
  stair-stepped at shot B's distance; 240x135 doesn't.
- **Runs:** `.codex-research/levelport/look/hazelight-*` (a: 4, L0.25 to L1.2 at 160x90, c at 240x135, d after
  `scaleVolFog`, final at the default); timings in `hazetime-*` and `hazetime2-*`.

## Facts that cost time

- **Spot direction.** Cyberpunk lights face their local +Y, not −Z. Aimed down −Z, the ring's floods lit the plaza
  instead of the towers.
- **Fog-only lights.** Lights with `sceneDiffuse 0` and `sceneSpecularScale 0` light only the fog. `Light_Distant`
  components are among them.
- **`useInFog`.** Light components carry it, nearly always 0: no light in the haze, whatever `scaleVolFog` says.
  The haze's glows come from static light nodes, among them the artists' own `*_Fog` lights.
- **CP's colour grade is one global LUT.** The weather states don't change it; the pollution weather's warmth is its
  fog and atmosphere. WolvenKit's PNG export garbles the 3D float LUTs: read `textureData` from the serialised .xbm.
- **Naming a debug-8 pixel.** The scene build writes `materials.json` beside its pk3 (index, mesh, material, base
  material): decode the pixel's index (4 bits per channel) and look it up there.
- **Honour `scaleVolFog`.** With every light in the haze, the ring's floods drew solid beams over the whole sky.
  The source gives each light a share (0, 50, 65, 85, 100 or 200 percent). The floods (`L_Zapiekanki_Main_Buildings`,
  7 spots) and nearly all ad lights have 0. The ring's halo is seven fog-only lights (`L_Zapiekanki_Main_Fog`, 40 m,
  20,000 lm) that the converter used to drop.
- **Spreads past 180 degrees are cones.** The ring's haze lights are 220-degree area lights facing outward. Drawn as
  spheres they filled the ring's middle with a white disc and veiled its underside.
- **The camera.** A player holding a saber is forced into third person, and the blade lights the scene: `look.py`
  shoots as a spectator. `setviewpos` pushes the view about 78 units along its yaw; a pitch is held look-up, so
  repeated shots shift by a pixel or two.
- **One look run at a time.** Four `look.py` clients started together gave black or missing screenshots.
- **Where the lamp and sign lights are.** Most are in archive entity templates (`fetch --ents`). Black instance lights
  are run-time placeholders.
- **Interior lights.** They are confined by light-channel volumes; the scene file stores a box per light (version 2).
- **The washed-out look was lighting calibration, not albedo.** The old `r_sceneDebug 1` passed albedo through
  exposure. Now 1 is the raw colour and 8 the material index (`docs/levelport-scene.md` lists them all).
- **Screens.**
  - `television_ad.mt` has the real ad in `AdTexture`, a sprite sheet.
  - UI-driven screens (`metal_base_ui.mt`) carry no content in the data.
- **Sky textures.** rd-vulkan shows sky textures about 30% darker than stored.
- **The comparison camera.** It can rise above the sky box (34 m) and then shows a grey sky. Players can't: the
  ceiling is 16 m.
- **Timing.** A single GPU timing run varies by ±0.4 ms, and absolute times drift by half a millisecond over an
  evening. Compare back to back on one view (`--cvar r_gpuTimes=1`; the `VKGPU` lines in the run's `qconsole.log`).
- **A light's own fixture shadows it.** Light shadow faces start a quarter metre out with depth clamp off, and
  emissive materials (lamp lenses) cast nothing.
- **Motion tests.** `look.py --set` lines run before the views, so a run of `setviewpos ... ; wait 1` pairs then
  `screenshot moving` captures a camera sweep (`ssrt-move`).
- **Run-around video** (2026-10-28). `python .codex-research/levelport/look/runvid2.py <out>`: the installed client
  records a demo of one steered lap of the plaza ring, then plays it back with `video` (1080p h264, 53 s, about
  85 s wall). It steers from logged `viewpos` lines through polled step files, along `kabuki_ring_route.npy` (the
  clearest loop on `kabuki_floormap.py`'s step-edge map). Open-loop turning (`runvid.py`, `runvid-a`) snagged on
  bollards and spiralled inward; a low brush counts as raised floor, so only step edges mark obstacles. The spawn
  saber must be holstered (`sv_saberswitch`; `weapon 1` lit it): a lit blade's reflection in the wet ground breaks
  into blocky blue sparkle (`runvid-b`). Clip: `runvid-c/home/base/videos/kabuki_run.mp4`.
- **Frame rate** (2026-10-28, dev renderer, RTX 3080, hidden window, vsync off). `look/timedemo.py` plays that lap
  with `timedemo 1` (1,086 frames, one per snapshot; `--gpu` adds `r_gpuTimes`). 1080p: 128.5 and 126.8 fps,
  GPU frame median 6.55 ms (p95 7.73). 4K: 78.1 and 77.8 fps, GPU 11.68 ms (p95 13.72). The moon's shadow
  cascades (`scene shadow`, the scene drawn into the sun cascades each frame) take 2.3–2.6 ms at either size, 40%
  of the 1080p frame (the lamp shadows are a load-time atlas); at 4K the scene pass (4.4 ms) leads.
  Runs `look/perf-*`. A demo runs no server or game logic, so live play costs a little more CPU.
- **Window rooms.** They lay out in mesh space. World space split windows, and the UVs repeat per window.
- **Debug views.** `r_sceneDebug` 9 shows wet (red) and puddles (yellow); 10 shows the reflections alone.
- **Shell traps.** Bash heredocs here eat backslashes, and `'\\0'` once became a NUL byte in `cp2gltf.py`. Long
  heredocs can also fail to parse at all, and a `\n` inside a C string written through one became a real line break.
  Write edit scripts to files with the editor and run them with Python. Python's writes to tracked files here
  sometimes fail with `Errno 22`; the file is left intact, so retry. `far` is a Windows macro: don't name a C++
  variable that.
