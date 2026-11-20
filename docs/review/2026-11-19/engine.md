# Engine (review 2026-11-19, master a322d9dc4)

Scope: `git diff 407a56fb master` over `codemp/qcommon`, `codemp/client`, `codemp/server`, `shared`, the four module
headers and the CMake files: 107 files, +7,322 / -399. Read in full: `cm_load.cpp` (3,449 lines), `cm_local.h`,
`cm_zbsp.cpp` (241), the `cm_trace.cpp` and `cm_test.cpp` changes, the `files.cpp` changes (+521), `cl_video.cpp`
(677), `cl_avi.cpp`, `cl_renderer_select.cpp` (283), `nj_input.cpp` (175), `cl_main.cpp`, `cl_cgame.cpp`,
`cl_cgameapi.cpp`, `cl_uiapi.cpp`, `cl_cin.cpp`, `snd_dma.cpp`, `snd_mix.cpp`, `sv_main.cpp`, `sv_client.cpp`,
`sv_ccmds.cpp`, `sv_gameapi.cpp`, `sv_init.cpp`, `bot2_servercost.cpp`, `common.cpp`, `cmd.cpp`, `shared/sys/*`,
`shared/sdl/sdl_window.cpp`, `tr_public.h`, `cg_public.h`, `ui_public.h`, `g_public.h`, every CMakeLists in scope.
`cl_cin_theora.cpp` (525) was reviewed 2026-10-07 (E1, E2, E4, E5 fixed) and only skimmed here.

Files with no findings: `cm_test.cpp`, `cm_trace.cpp`, `cm_local.h`, `cm_public.h`, `cm_polylib.h`, `cmd.cpp`,
`cl_cin.cpp`, `cl_cin_theora.cpp`, `cl_console.cpp`, `cl_input.cpp`, `cl_uiapi.cpp`, `client.h`, `snd_dma.cpp`,
`sv_ccmds.cpp`, `sv_gameapi.cpp`, `server.h`, `q_math.c/h`, `qfiles.h`, `con_win32.cpp`, `sys_unix.cpp`,
`sys_win32.cpp`, `sdl_window.cpp`, `cg_public.h`, `ui_public.h`, `tr_public.h` (one comment, RE-22), the CMake files,
`shared/icons`.

Checked and found sound (the brief's three questions):

- **Chunk worker thread safety.** `cm_jobQueue`, `cm_jobDone` and `cm_freeQueue` are only touched under `cm_jobLock`
  (worker 1804-1863, flush 1943, retire 1963, take 2013, abandon 2041, the residency re-order 3258, shutdown 1893 and
  1900). A job belongs to the worker from the moment it leaves the queue and to the main thread once it is in
  `cm_jobDone`; `job->dist` is only rewritten for jobs still queued, under the lock. The slots (`cm_chunks`), the
  manifest, the grid and every `cm_slot*` array are main-thread only; the swap into a slot happens only in
  `CM_ChunkResidency` / `CM_SwapPreloadJobs`. The parse on the worker reaches no `Com_Error`, `Com_Printf`, cvar,
  zone or hunk call: `CM_ValidateChunk` rejects every input the lump loaders would error on (one model, no surfaces,
  so no `CM_GeneratePatchCollide` and no submodel "leaf" allocations), `CM_Alloc` uses `calloc` and throws
  `cmParseFail`, `CM_FloodArea_r`'s `Com_Error` cannot fire on a fresh clip map (its portals are all zero). zlib is
  built without `DYNAMIC_CRC_TABLE` and inflates with its own allocator; zstd contexts are per call and
  `ZstdLoad` is locked. No build flag disables exceptions.
- **Map change.** `CM_ClearMap` -> `CM_ClearChunks` drops queued and staged jobs, bumps `cm_jobGeneration`, retires
  resident chunks to the worker and frees the manifest and grid; a job in flight lands with the old generation and is
  retired, never swapped in (3067, 3286). `SV_ClientEnterWorld` resets the per-client hold state. Leftovers: RE-15,
  RE-16.
- **32/64-bit.** The collision records are 32-bit indices on both builds; `cLeaf_t.firstLeafBrush` as `int` is safe
  for the hub and sub-BSPs (both arrays are on the one hunk block) and never used for a chunk (one model). Appended
  import/export entries are pointers at the end of their tables. Findings: RE-10, RE-17, RE-18.

## Findings

### RE-1 P1 `cl_fixSimultaneousInputs` drops the queued commands whenever a frame passes without a packet
- Where: `codemp/client/nj_input.cpp:107-163` (the `njPending = 0` at 162), `codemp/client/cl_input.cpp:1618-1623`
- Scenario: a remote server, `com_maxfps` above `cl_maxpackets` (the normal case), a bind `force_speed;
  force_protect`. Frame 1: both desired, speed goes into the usercmd, `njPending = {protect}`,
  `cl.gcmdSentValue = qtrue`, `cl.gcmdValue = 0`. No packet is due, so `CL_WritePacket` does not clear
  `cl.gcmdSendValue`. Frame 2: `gcmdSendValue` is still true and `njDesiredThisFrame` is 0, so neither branch at
  96-105 runs; the block at 107 counts 0 commands, takes the `else` at 145, finds `cl.gcmdValue == 0` and no desired
  bit, and line 162 clears `njPending`. Protect is never sent. On a listen server or LAN (`CL_ReadyToSendPacket`
  always true) the packet goes out every frame and the feature works, which is where it was tried.
- Fix: in `NJ_FinishMoveGenCmd`, when `cl.gcmdSendValue` is set but nothing was pressed this frame
  (`!njDesiredThisFrame && !cl.gcmdValue`), leave `njPending` alone and return (the frame-1 command is already in its
  usercmd); pop the next pending command only once `gcmdSendValue` has been cleared by the packet.
- Confidence: likely (traced in code; not run against a remote server)
- Status: fixed (80e55a3e): a frame with no new press while the chosen command waits for its packet keeps the pending ones; needs a remote-server run to confirm

### RE-2 P1 `s_volume 0` now mutes music and voices as well as effects
- Where: `codemp/client/snd_mix.cpp:262-264`
- Scenario: the stock "Effects volume" slider at 0 with music on (`s_musicvolume 0.5`). Stock scales only the effect
  channels by `s_volume`; music (`s_rawsamples`, 443-457) and voices keep their own levels. Now the whole paint
  buffer is zeroed, so the player loses music and voice lines. The comment says it is deliberate (a silent capture
  run), but it changes a stock setting's meaning for every player with the DMA mixer.
- Fix: keep stock semantics and give captures their own switch (a `cl_videoMuteSpeakers`/`s_mute` cvar tested here
  instead of `s_volume <= 0`).
- Confidence: confirmed (traced); the intent is the owner's call
- Status: fixed (80e55a3e): owner decision 2026-11-19: s_volume 0 mutes the effects only, as stock; new cvar s_mute (default 0, not archived) silences the whole DMA mix and a recording still takes its sound

### RE-3 P2 The dedicated server's Ghoul2 transform heap is 1 MB, the listen server's 8 MB
- Where: `codemp/server/sv_init.cpp:774-779` (1024 KB), `codemp/client/cl_main.cpp:2470-2472` (8192 KB, under a
  comment that says "to 1 MB to match sv_init.cpp"), `codemp/rd-vanilla/G2_misc.cpp:400-404, 622-625`
- Scenario: a dedicated server, a saber or missile Ghoul2 trace against a player model whose traced LOD has more
  than ~52,000 vertices (20 bytes a vertex): `MiniHeapAlloc` returns NULL and `G2_TransformModel` raises
  `ERR_DROP "Ran out of transform space"`, which ends the map on the server. The same model works on a listen
  server. The listen value was raised twice past its comments, which suggests the limit was met there.
- Fix: one constant in a shared header for both builds (8192), comments corrected.
- Confidence: needs a run (depends on the vertex count of the installed models at the trace LOD)
- Status: fixed (80e55a3e): G2_VERT_SPACE_SERVER_SIZE 8192 in qcommon/MiniHeap.h for both builds, comments corrected

### RE-4 P2 `SV_Frame` stops catching up while any command text is pending, a `wait` included
- Where: `codemp/server/sv_main.cpp:1285-1294`, `codemp/qcommon/cmd.cpp:176-178`
- Scenario: a listen server at `sv_fps 40` whose client frame takes longer than 25 ms, with a script holding a
  `wait` in the command buffer (a looping bind, a cfg with `wait 200`). `Cbuf_Pending()` is true every frame until
  the wait ends, so each engine frame runs one game frame: the game runs slow, `sv.timeResidual` grows, and the
  backlog runs in one burst when the buffer empties. The break exists so a command queued by a game frame runs
  before the next one; a waiting buffer cannot run anyway.
- Fix: break only when the buffer is runnable (`Cbuf_Pending() && !cmd_wait`), or compare `cmd_text.cursize` before
  and after `GVM_RunFrame` so only text the frame itself queued breaks the loop.
- Confidence: likely (traced; not run)
- Status: fixed (80e55a3e): Cbuf_Pending is false while a wait holds the buffer

### RE-5 P2 A fatal error exits through `std::terminate`: `fs_prefetchThread` is still joinable at `exit()`
- Where: `codemp/qcommon/files.cpp:3411` (a static `std::thread`), `codemp/qcommon/common.cpp:327-329`,
  `shared/sys/sys_main.cpp:210-226`
- Scenario: any `Com_Error( ERR_FATAL )` after the prefetch worker has started (rd-vulkan's model warm-up starts it
  in normal play). The fatal path calls `Com_Shutdown` and `Sys_Error` but not `FS_Shutdown`, so `exit()` destroys a
  joinable `std::thread`: `std::terminate`, `abort`, a second crash report after the error dialog and exit code 3
  from the wrong place. `Com_Quit_f` calls `FS_Shutdown` and is fine. The chunk worker avoids this (a heap pointer,
  joined in `Com_Shutdown`).
- Fix: call `FS_PrefetchShutdown` from `Com_Shutdown` (export it), or hold the thread as `std::thread *` like
  `cm_worker`.
- Confidence: confirmed (traced) for the path; the visible effect needs a run
- Status: fixed (80e55a3e): FS_PrefetchShutdown exported and called from Com_Shutdown

### RE-6 P2 The renderer start mark turns a closed or second launch into a permanent switch to rd-vanilla
- Where: `codemp/client/cl_renderer_select.cpp:215-238` (read), `263-273` (write), `276-283` (clear)
- Scenario A: the player closes the window or kills the process during `BeginRegistration` (a slow first launch
  after a build). Scenario B: a second branded client starts while the first is still inside `BeginRegistration`
  with the same home (the branded client's home is the install folder). Either way the next start reads the mark,
  prints one warning, starts rd-vanilla and writes `xenjk_renderer rd-vanilla` to the config, where it stays until
  the player sets it back by hand.
- Fix: put the process id in the mark and ignore it while that process is alive; fall back for that launch only and
  change `xenjk_renderer` after a second consecutive unfinished start.
- Confidence: likely (traced; not run)
- Status: fixed (80e55a3e) for scenario B: the mark holds the process id and a mark whose process is alive is left alone; deferred for scenario A (a closed first launch still switches): falling back for one launch only and switching after a second unfinished start is a design choice

### RE-7 P2 `r_modelWarmupApi` outlives the renderer that set it
- Where: `codemp/client/cl_main.cpp:2178, 2189, 2195`, `codemp/client/cl_cgameapi.cpp:1916`,
  `codemp/client/cl_uiapi.cpp:1432`, set at `codemp/rd-vulkan/tr_init.cpp:1211`
- Scenario: rd-vulkan sets the ROM cvar to 2; the player switches `cl_renderer` to a renderer library built against
  the stock `refexport_t` (a stock OpenJK or other fork's rd-vanilla/rend2 dropped in) and does `vid_restart`. The
  cvar is still 2, so the engine reads `re->WarmModel`, `re->WarmState` and `re->WarmModelEx` past the end of that
  library's smaller export table and may call what it finds. The in-tree renderers are safe (same header, NULL
  entries). `CL_ModelWarming` also calls `re->WarmState` without the NULL test the two bind sites make.
- Fix: in `CL_InitRef`, force `r_modelWarmupApi` to 0 before `GetRefAPI` so only the renderer just loaded can
  advertise it; test `re->WarmState` at 2178.
- Confidence: confirmed (traced) for the stale cvar; the crash needs a foreign renderer
- Status: fixed (80e55a3e): CL_InitRef clears r_modelWarmupApi before GetRefAPI and marks it user-set so the renderer's CVAR_ROM Cvar_Get sets it again; CL_ModelWarming tests re->WarmState

### RE-8 P2 Stored-entry reads keep one extra `FILE` per pack open until `FS_Shutdown`, with no bound
- Where: `codemp/qcommon/files.cpp:3430-3441` (`FS_RangePack`), used by `FS_ReadStored` 3446-3463, `FS_RangeRead`
  3728, the worker 3486
- Scenario: `fs_storedFast 1` (default). Every pack from which a stored entry is read gets a second stdio stream on
  the main thread (the unzip handle is the first), and a third on the worker once a range or prefetch touches it.
  MSVC's stdio allows 512 streams; an install with a few hundred packs with stored entries (character and scene
  packs are stored) runs out, after which `fopen` fails for whatever comes next: later packs, logs, screenshots,
  `CM_ZipRead`. The fast paths themselves degrade cleanly; other callers do not.
- Fix: cap each map (for example 32 handles, least recently used closed), or read through the pack's existing
  handle on the main thread.
- Confidence: likely (needs a count of packs on a full install)
- Status: fixed (80e55a3e): each range map keeps at most 32 open packs (the oldest-named is closed first); needs a full-install run to confirm the stream count

### RE-9 P2 A manifest with more than 1,024 chunks is cut off without a word
- Where: `codemp/qcommon/cm_load.cpp:2412-2414`
- Scenario: `maps/<map>.chunks` with 1,100 lines. The loop breaks at `MAX_CM_CHUNKS`; the last 76 chunks are not
  slots, not unloaded boxes and not loaded: players fall through that part of the map. The log line reports
  "1024 chunks" and nothing else.
- Fix: count the lines past the limit and print a red warning (or refuse the manifest).
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): the lines past the limit are counted and reported in red

### RE-10 P2 The automatic `cm_chunkMegs` cap ignores a 32-bit process's address space
- Where: `codemp/qcommon/cm_load.cpp:2628-2648`
- Scenario: the 32-bit client or server on a 32 GB machine: the cap is 45% of physical memory, 14.4 GB, in a 4 GB
  (large-address-aware) process that also holds the hunk and the renderer. The cap never evicts; `calloc` fails
  first, every failed chunk is "out of memory, tried again in 5 s" (2603), and chunks beyond reach are still
  resident.
- Fix: `if ( sizeof( void * ) == 4 ) cap = min( cap, ~1 GB )`.
- Confidence: confirmed (traced) for the arithmetic; the failure needs a large map on the 32-bit build
- Status: fixed (80e55a3e): at most 1 GB on a 32-bit build; cvar text updated

### RE-11 P3 `SV_ChunkFrame` clears svFlags bit 0x10000 on every active client on every map
- Where: `codemp/server/sv_main.cpp:1165-1175`, `codemp/game/g_public.h:58`
- Scenario: a game module from another fork that uses 0x00010000 for a flag of its own, on a map with no chunks:
  the engine clears it every game frame.
- Fix: return before the hold loop when no manifest is loaded (a `CM_HasChunks()` query), which also saves the loop.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): CM_HasChunks(); SV_ChunkFrame leaves the hold loop (and the bit) alone on a map without chunks

### RE-12 P3 `sv_chunkHoldFlag 0` does not make a held client "move anyway"
- Where: `codemp/server/sv_main.cpp:1155, 1168`, `codemp/server/sv_client.cpp:1380`
- Scenario: the control run sets the cvar to 0: the flag and `PM_FREEZE` are skipped, but `SV_ClientThink` still
  returns on `SV_ChunkHeld`, so a human client's usercmds are dropped; only game-run bots move. The cvar's text says
  otherwise.
- Fix: test the cvar in `SV_ClientThink` too, or correct the description.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): SV_ChunkHoldEnabled() tested in SV_ClientThink too

### RE-13 P3 A pack with a zip64 directory sends every chunk through a failed worker job and a load in the frame
- Where: `codemp/qcommon/cm_load.cpp:1444-1448`, `3275-3284`
- Scenario: a chunk pack past 4 GB or with 65,535 entries or more: each chunk costs a worker read of the tail, a
  yellow warning, then `CM_LoadChunkSlot` in the server frame (the 387 ms frame the comment at 1479 records).
- Fix: read the zip64 end-of-central-directory locator in `CM_ZipRead`, or refuse such a pack once with one line.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): CM_ZipRead reads the zip64 end-of-central-directory locator and record; not exercised (no pack past 4 GB or 65,535 entries in a test)

### RE-14 P3 `CM_ZipRead` re-reads and scans the pack's whole directory for every chunk
- Where: `codemp/qcommon/cm_load.cpp:1454-1476`
- Scenario: a pack with tens of thousands of entries: megabytes of directory read and a linear name compare per job.
- Fix: keep the last pack's name -> (offset, sizes, crc) table on the worker.
- Confidence: confirmed (traced); small
- Status: deferred: a per-pack directory cache on the worker is a performance change with an invalidation rule to choose; measure on a large pack first

### RE-15 P3 One finished chunk stays allocated after the last map is cleared
- Where: `codemp/qcommon/cm_load.cpp:2038-2054`, `3120-3123`
- Scenario: `killserver` or a client disconnect while the worker is parsing: the job lands in `cm_jobDone` after
  `CM_AbandonChunkJobs`, and nothing takes it until the next `CM_ChunkResidency` call, which a client at the menu
  never makes. One chunk's blocks (possibly hundreds of MB) stay until the next map.
- Fix: have the worker free a result whose generation is stale itself (read `cm_jobGeneration` under the lock).
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): the worker frees a result whose generation is stale instead of queuing it

### RE-16 P3 A manifest file with no valid line leaks its table on the next read
- Where: `codemp/qcommon/cm_load.cpp:2383`, guards at 2438 and 2462
- Scenario: `maps/x.chunks` exists but yields 0 entries: `cm_manifest` is allocated with `cm_manifestCount` 0, the
  guards test the count, and the listen server's client load (or `cm_chunkmanifest`) allocates another 100 KB table
  over it.
- Fix: free `cm_manifest` when `n == 0`, or guard on the pointer.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): the table is freed when no line is valid

### RE-17 P3 The prefetch worker seeks with a 32-bit `long`
- Where: `codemp/qcommon/files.cpp:3505, 3510, 3523`, sizes in `unsigned long` 3501-3517
- Scenario: Windows, a pack over 2 GB: `fseek( f, (long)offset )` gets a negative offset, fails, and every entry past
  2 GB is skipped: no read-ahead for it, no message. `FS_RANGE_SEEK` (64-bit) is defined a few lines above and used
  by the range reads.
- Fix: use `FS_RANGE_SEEK` and `unsigned long long` in `FS_PrefetchPack`.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): FS_RANGE_SEEK in FS_PrefetchPack (the zip32 fields stay 32-bit)

### RE-18 P3 `CM_XbszDecode`'s frame bound check wraps on a 32-bit build
- Where: `codemp/qcommon/cm_zbsp.cpp:210`
- Scenario: a crafted XBSZ file (a downloaded pack) with `m` near 4 GB: `p + m` wraps in a 32-bit `size_t`, passes
  the check, and zstd is handed a length past the buffer.
- Fix: `m > (size_t)len - p`.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e)

### RE-19 P3 A changed model holds every other userinfo change for up to `cl_modelWarmTimeout`
- Where: `codemp/client/cl_main.cpp:2217`
- Scenario: the player changes model and name (or Force powers before a team change) together: nothing is sent
  until the warm-up ends, up to 10 s.
- Fix: send the userinfo with the applied model's value while a warm-up is pending, then again with the new model.
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): while a model warms, a userinfo with the server's model goes once if anything else differs (the server allows three changes in six seconds, so nothing is sent for a model-only change); needs a run

### RE-20 P3 `stopvideo; video name` in one frame records nothing
- Where: `codemp/client/cl_main.cpp:2737-2741`, `2728`, `codemp/client/cl_avi.cpp:386`
- Scenario: a script stops one recording and starts the next: the stop is deferred to the end of the frame, so
  `CL_OpenAVIForWriting` returns qfalse on `afd.fileOpen` and `CL_Video_f` says nothing.
- Fix: in `CL_Video_f`, close a pending stop first (or print that a recording is still closing).
- Confidence: confirmed (traced)
- Status: fixed (80e55a3e): CL_Video_f closes a pending stop first, and says so when a recording is still open

### RE-21 P3 The branded-binary test reads `argv[0]`
- Where: `shared/sys/sys_main.cpp:91-101, 802`
- Scenario: a launcher that passes a different `argv[0]` than the file it starts, or a symlink named otherwise: the
  renderer choice and the portable home follow the name given, not the binary.
- Fix: take the name from `GetModuleFileName` / `SDL_GetBasePath`-style lookups, `argv[0]` as the fallback.
- Confidence: confirmed (traced); unusual launchers only
- Status: fixed (80e55a3e): Sys_ExecutablePath (GetModuleFileName, /proc/self/exe), argv[0] as the fallback

### RE-22 P3 Small costs and wording
- `codemp/client/cl_renderer_select.cpp:245-254`: the Vulkan probe (load the loader, create an instance, enumerate)
  runs on every `vid_restart`; probe once a process. Line 240-241: after a probe fall back, the next `vid_restart`
  writes rd-vanilla into `xenjk_renderer`.
- `codemp/qcommon/cm_load.cpp:2304-2313`: every world trace and point contents on a chunked map walks all manifest
  slots for `cm_slotAlways` and sorts the candidates; keep a short list of always-slots.
- `codemp/client/cl_main.cpp:2328`: `CL_VideoPipe_Failed()` locks a mutex every client frame; test `vp.active`
  first (`cl_video.cpp:548-552`).
- `codemp/client/cl_video.cpp:100-101`: `cl_videoFFmpeg` and `cl_videoArgs` name a program to run and its
  arguments; mark them `CVAR_PROTECTED` so no module or server can set them.
- `codemp/rd-common/tr_public.h:381-383`: "FS_RangeRead (this thread)" reads as any thread; it uses the unguarded
  `fs_rangeFiles` and `fs_rangeMainFiles` and is main-thread only. Say so.
- `codemp/client/cl_renderer_select.cpp:205`: the cvar text names `xenjk.x86.exe`; the 64-bit client is
  `xenjk.x86_64.exe`.
- `codemp/client/cl_main.cpp:2997`: "Stop avi recording" (the default output is now mp4).
- Status: fixed (80e55a3e): the probe runs once a process; a probe fall back is not written to xenjk_renderer; CL_VideoPipe_Failed tests vp.active before the lock; cl_videoFFmpeg and cl_videoArgs are CVAR_PROTECTED; tr_public.h says main thread only; the cvar text says xenjk.*; "Stop video recording". Deferred: the always-slots list in CM_ChunkCandidates (a performance change; measure first)

## Publish cleanliness

No absolute local paths, user names, private URLs or credentials in the lane's additions (searched the whole diff).

- `codemp/qcommon/cm_zbsp.cpp:1`, `cm_zbsp.h:1`, `codemp/client/cl_video.cpp:1`,
  `codemp/server/bot2_servercost.cpp:1`, `bot2_servercost.h:1`: no GPL-2.0 header (the tree's other new files have
  one: `cl_renderer_select.cpp`, `nj_input.cpp`). Add it.
  - Status: fixed (80e55a3e)
- `codemp/qcommon/cm_load.cpp` (37 places with `sv_client.cpp`, `sv_ccmds.cpp`, `cl_cgame.cpp`): comments cite
  private process records: "the review's D5", "round 2 review D3", "Codex's round 2 #5", "run 6's 77 ms", "tag 3's
  2.5 GB pack", "r3col smoke B", and `mem/GATE-preload.md` (2848, 3003; `sv_client.cpp:560`), a file not in the
  repository. Replace with the reason, or with a `docs/levelport-collision.md` section.
  - Status: deferred: comments citing records go to the separate comment sweep (contract); `mem/GATE-preload.md` is now in `sv_chunk.cpp` and `cm_chunk.cpp`
- `codemp/client/nj_input.cpp:30`, `codemp/client/client.h:348`, `codemp/CMakeLists.txt:644`: "owner, 2026-10-08",
  "owner 2026-10-07", "the owner's firewall rule". Reword for outside readers.
  - Status: deferred: comments with owner dates go to the separate comment sweep
- `codemp/client/cl_main.cpp:2470-2471, 2769-2772`: comments say 1 MB and 4 MB; both values are 8192 KB.
  - Status: fixed (80e55a3e)
- `codemp/qcommon/cm_load.cpp:112-137, 1247-1265`: dead since every chunk goes through `CM_ParseChunk`:
  `CM_Alloc`'s zone branch, `cm_workerAlloc` (always equal to `cm_loadingChunk`), `cmChunk_t.malloced` (always
  true) and `CM_FreeChunk`'s `Z_Free` branch; the `cm_loadingChunk` tests at 220 and 924 can no longer be true in
  `CM_LoadMap_Actual`. The header comment at 112-118 still describes zone blocks.
  - Status: fixed (80e55a3e): cm_workerAlloc, the zone branches and cmChunk_t.malloced removed, as were the cm_loadingChunk tests in CM_LoadMap_Actual and CM_LoadMapChunks. Not a bug at 220: CMod_LoadSubmodels runs for chunk parses too, where the test is live
- `codemp/qcommon/cm_load.cpp:2086, 3128, 3129`, `sv_main.cpp:1155`: test fault cvars in the shipped engine
  (`cm_chunkTestFailMem`, `cm_chunkTestDelayMs`, `cm_chunkLoadByCentre` with a hard-coded 4,800 u tile,
  `sv_chunkHoldFlag`), with `cm_drive`, `cm_drive_move` (`common.cpp:1243-1244`). All cheat-protected; put them
  behind one build define or leave them and say they are test hooks in `docs/levelport-collision.md`.
  - Status: deferred: owner decision (test hooks behind a build define, or documented in docs/levelport-collision.md, which is another lane's)
- `codemp/server/bot2_servercost.cpp`: a research timing hook compiled into every server (`bot_moveCostRecording`,
  writes `server_movement_cost.csv`), one statement a line in a style unlike the tree. Build-flag it or remove it.
  - Status: deferred: owner decision (whether the opt-in research hook ships); licence header added (80e55a3e)
- `codemp/qcommon/cm_load.cpp:2586, 3125` (`cm_chunkLoadDist`), `2865, 3127` (`cm_chunkAsync`), `2829-2830` and
  `sv_client.cpp:575-576` (`cm_chunkSpawnNear`, `sv_chunkFirstEntryNear`): the same cvar registered in two places
  with its default and description typed twice. One registration function.
  - Status: fixed (7f9e6ba3): each registered once in cm_chunk.cpp (CM_ChunkSpawnNearCvar and CM_ChunkFirstEntryNearCvar are shared with sv_chunk.cpp)
- `CMakeLists.txt:96`: "built and played on MSVC in review area 1, 2026-10-07": private reference.
  - Status: deferred: comment sweep
- `codemp/client/FxUtil.cpp:115-116`: the engine registers `cg_cameraShake` (a cgame-prefixed name) and defaults it
  and `fx_screenFlash` to 0, so effect-file shakes and flashes are off under any cgame. A stated choice; list it
  among the changed stock defaults for publication.
  - Status: not changed: listed among the changed stock defaults (cg_cameraShake and fx_screenFlash default 0)

## Liftability

- **Collision chunks.** About 2,300 lines of residency, worker, zip reader, manifest, grid and report live inside
  `cm_load.cpp` between the stock loaders. Proposed shape: `cm_chunk.cpp` (worker, `CM_ZipRead`, validate/parse,
  slots, residency, commands, the cvar registrations in one `CM_ChunkInit`) with `cm_chunk.h`; `cm_load.cpp` keeps
  the lump loaders and exposes `CM_Alloc` and the loaders it shares. Hooks that remain in stock code: one call in
  `CM_LoadMap` and `CM_ClearMap`, one in `CM_BoxTrace` and `CM_PointContents`, `SV_ChunkFrame` (move it and
  `SV_ChunkHeld` to `sv_chunk.cpp`), one line in `SV_ClientThink`, one block each in `CL_Frame` and
  `CL_GetSnapshot`.
  - Status: done (7f9e6ba3): cm_chunk.cpp + cm_chunk.h (cm_load.cpp keeps the lump loaders, CM_Alloc and CM_ParseChunk); sv_chunk.cpp holds SV_ChunkHeld, SV_ChunkHoldEnabled and SV_ChunkFrame. Not done: the CL_Frame and CL_GetSnapshot blocks (client files, not a move), and one CM_ChunkInit for every cvar (the lazy registrations stay where they are read)
- **Compact collision records.** The index-based `cNode_t`/`cbrush_t`/`cbrushside_t`/`cmPlane_t` rewrite is a
  separate feature from streaming but ships in the same files; a derivative that wants chunks must take both. Note
  the dependency in `docs/levelport-collision.md`, or keep the record change as its own commit when publishing.
  - Status: not done: documentation in docs/levelport-collision.md (another lane) or a publishing-time commit split
- **Module API additions.** Six advertising cvars (`win_vulkan`, `cl_videoCaptureHooks`, `fs_prefetchApi`,
  `r_modelWarmupApi`, `cl_cgameWarmApi`, `cl_uiWarmApi`, `sv_g2ClockImport`) guard appended table entries. It works,
  but each is a string anyone can create on an engine without the entry, and one outlives its renderer (RE-7).
  Proposed shape: one `xenjk_api.h` listing the names and levels, set and cleared in one place per table
  (`CL_InitRef`, `CL_BindCGame`, `CL_BindUI`, `SV_BindGame`).
  - Status: not done: a design (one xenjk_api.h and one set/clear place per table), not a move; RE-7 fixed the renderer case
- **Pack prefetch and range reads.** 460 lines inside `files.cpp` above `FS_Shutdown`. Move to `fs_range.cpp`; it
  needs only `fs_searchpaths`, `fsh[].pakFilename` and the unzip handle, which a small accessor can give
  (`FS_StoredEntry( handle, &pakPath, &start )`). `FS_PakPathOf` belongs with it.
  - Status: not done: files.cpp's private types (fileHandleData_t, searchpath_t, pack_t) and FS_ReadStored's use of fsh[] would need a new private header or accessors: not a pure move
- **Video capture.** `cl_video.cpp` is self-contained; the tangle is in `snd_mix.cpp` (`fxpaintbuffer`,
  `snd_paintDest`, the `s_volume` rule of RE-2) and in `cl_avi.cpp`'s `afd.pipe` branches. The sound part could be
  one function `S_CaptureMix( endtime )` called from `S_TransferPaintBuffer`.
  - Status: not done: the capture mix is already one call (S_MixVideoCapture); the rest is in the paint path, not mechanical
- **Branded client.** The `xenjk.*` name decides the renderer (`cl_renderer_select.cpp`), the home path
  (`files.cpp:4053-4055`) and the crash log path (`sys_main.cpp:237-239`); the copy is made only on Windows
  (`codemp/CMakeLists.txt:646`). Proposed shape: a build option (`-DXENJK_BRANDED`) or one `Sys_IsBrandedBinary`
  consumer function `Com_BrandedDefaults()`; build the copy on every platform or say it is Windows only.
  - Status: not done: a design choice (build option or one consumer function)
- **Bot2 in the engine.** `bot2_servercost.cpp` and the `Cbuf_Pending` break (RE-4) exist for Bot2 replays; both sit
  in `SV_Frame`. Gate them with one cvar or define so a derivative can take the server loop unchanged.
  - Status: not done: owner decision (gating Bot2 replay support)

## Still open from earlier reviews

- E8 (`tr_types.h:304` `skin_t.surfaces[1280]`): still as reported.
