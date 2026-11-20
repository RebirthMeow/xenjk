# Map load time (rd-vulkan, 2026-10-14)

Owner: "map loads seem kind of long", then "do the map load evaluation".

## Method

- `r_loadProfile 1` (rd-vulkan, default 0) times the renderer's load phases from `RE_LoadWorldMap` on. `loadprofile`
  prints them as `LOADPROF` lines.
  - Phases: world (all of `RE_LoadWorldMap_Actual`), light directions, probe setup (entities, box fitting, blend
    sets), VBOs, probe bake (`R_RenderAllCubemaps`), images (outermost `R_FindImageFile` calls), pipelines created.
  - Images overlap world: the BSP's shaders and lightmaps load their images inside it.
- Whole load: from the log's `Server Initialization` to `CL_InitCGame: N seconds` (the cgame's media registration,
  which includes the world load).
- Run: `.codex-research/renderer-bench/scenes/20261014T034832-best-hidden-loadprof`. Profiles `best,hidden,loadprof`,
  candidate `xenjk-vk-devdll-cs` (the installed exe, dev rd-vulkan), two reps per map back to back.
  - Rep 0 is the first load of the map in that session: its files come off D:, a 4 TB Seagate HDD (C: is a
    2 TB WD SN850X NVMe SSD, 102 GB free).
  - Rep 1 follows within a minute: its files come from the Windows file cache.

## Results

| | kejim rep 0 | kejim rep 1 | dantooine_pbr rep 0 | dantooine_pbr rep 1 |
|---|---|---|---|---|
| Server Initialization to cgame done | 27 s | 7 s | 34 s | 15 s |
| CL_InitCGame | 21.9 s | 4.3 s | 31.5 s | 10.9 s |
| world (BSP, shaders, lightmaps) | 3.3 s | 0.7 s | 26.3 s | 5.0 s |
| images (862 / 1031 lookups) | 8.3 s | 1.4 s | 14.9 s | 4.2 s |
| probe bake (12 / 29 probes) | 0.45 s | 0.38 s | 0.71 s | 1.32 s |
| probe setup incl. box fitting | 0.03 s | 0.02 s | 0.07 s | 0.07 s |
| pipelines created (69 / 97) | 0.30 s | 0.14 s | 0.98 s | 0.16 s |
| light directions, VBOs | < 0.03 s | < 0.03 s | < 0.03 s | < 0.03 s |

- **Disk, not the renderer's new work.** A cold load is 3–5x a warm one: 17–20 s of each first load is reading the
  game's files from D:. The probe bake is under 1.5 s and the box fitting under 0.1 s, so a probe cache would save
  at most about a second.
- **Warm residual:** images are the largest part (1.4 s on kejim, 4.2 s on the PBR map, which carries normal and
  specular maps), then the PBR map's world (5 s, its shaders' images).
- Pipelines: the driver's own cache keeps creation under 1 s even on the first load.

## Options

1. Install the game on an SSD. This removes most of the cold time; no code.
2. Decode images on worker threads, keeping upload on the main thread. This cuts the warm image time and, with
   reads overlapped, part of the cold time.
3. A compressed-texture cache (BC7/BC5 written once per image) so loads read smaller files and skip decode and
   mipmapping.
4. Probe cache: not worth it on these numbers.
