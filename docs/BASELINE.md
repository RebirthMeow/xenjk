# Baseline 1: one tree, one clean import (plan, 2026-11-06)

The owner, 2026-11-06: "create a baseline ... make sure all our fixes are hitting everything, full clean import/tiles,
all commit and features from other workstreams ... a really solid foundation for us to then continue dev on".

**Done means:** one commit from which every piece the owner plays is rebuilt by script: exe, renderer, game modules,
the Watson scene, its sidecars and collision, and the kit. No one-off patch scripts, no overlay packs, no binaries
from side trees. Each fix is shown to reach every tile, not only the view it was found at. This is a plan to run, not
a record. INTEGRATION.md is still the older plan for gameplay branches B and C; this plan takes
them in.

## Where things are today

Code: eleven places, none of them the tree the kit runs.

| What | Where | State |
| --- | --- | --- |
| Main line | `renderer/pbr-materials` ad5d22b1 | Installed line. Uncommitted: nit fix (cp2gltf), multilayer levels (cp77mat), cp77decals.py, README, docs. |
| Renderer H14-H44 (the kits' renderer) | `.codex-research/wt-stream`, branch `wip/stream` | 2 commits (H12, H13b) on 83b7b748, 4 behind the main line. **H14-H44 are uncommitted**: 30 builds' working tree, sources in look/stream-20261104/src/post_hNN. |
| Water round 2 | `wip/water` | 1 commit, not merged. |
| Renderer experiments | `wip/r8`, `wip/v5`-`v8`, `wip/viewcull`, `wip/p3-*`, `wip/standin`, `collision/world128` | Some adopted into wt-stream by hand (debug 15/16, stand-ins, decals?), some not. Unknown which until diffed. |
| Collision | `collision/r3-residency` (4 ahead), `collision/m2` (stopped, evidence) | Phase 2 session (district map under 2 GB) idle. |
| Gameplay round 3 | `claude/r3-gameplay-fixes-212fe2` | 6 ahead: bike pitch/lean, picker, pk3 reads. |
| UI | `wishlist/xenui` (7 ahead), `wishlist/player-screen` (3 ahead + 9 dirty files) | Player-screen crisp-face session idle. |
| Tick independence (B) | `gameplay/tick-independence` b34547a6 | 1 commit, 209 behind. Saber 125 Hz open. |
| Bot2 horizons in ms (C) | `claude/vigilant-sanderson-aa0512` 49a9eb0f | 1 commit, 209 behind. |
| Engine in the kits | from an older tree (kit notes) | The stream tree's engine-only build compiles but has never been run. |

Watson data: scene 10j (tiles 10h, 268 tiles, 38 GB) built before today's converter, plus eight overlays keyed to its
records (zshape, zvb, zfol, zholo, ads, xscr, zznit, zzzmlrough) and eleven patch scripts in cp77/watson. The test
build r3ds (one tile, today's converter) shows that xdet, xfog, xlut, xman, xrot, xscr, xwat and materials now come out
of the converter. Collision chunks, decals, effects, env packs, interiors and the ocean come from separate tools, run
by hand.

## Phases

### 0. Freeze (an hour; the owner's word for each commit)

- Each side session finishes its current step or stops; nothing new starts until phase 1 is merged.
- Commit every branch as it stands, each on its own branch: the wt-stream tree as H14-H44 (one commit per round
  from the post_hNN sources, so history keeps the steps), player-screen's 9 files, the level port's uncommitted work.
- Diff wt-stream against each `wip/*` and `collision/world128` branch: a list of what was adopted and what was not.
  Anything not adopted stays out of the baseline unless the owner says otherwise.

### 1. One line (half a day)

- Branch `baseline/1` from the main line. Merge in this order, each followed by a clean build:
  stream renderer, water, collision r3-residency, gameplay round 3, xenui, player-screen, C (Bot2), B (tick).
- Build fresh, from this tree only: exe, renderer (SPIR-V regenerated), cgame/ui/jampgame, x86 and x64.
- Gates: every tree compiles warning-clean; tools/tick_rules_sim.py; the Python suite from the repo root; levelcheck's
  tests; renderer load checks (20 loads, no crash) on night-29's kit with the new binaries; the bike and picker
  smokes; one Bot2 laps world. No full movement gates (owner 2026-10-06).

### 2. Every fix in the converter (half a day)

- **Parity check per class**, tile -7_4: r3ds (today's converter, no overlays) against night-29 (10j + overlays),
  records matched by position and material name. Lights (power, shape, type flags, nit, channels, variants), materials
  (flags 0x200/0x400/0x800, emissive, screens, holograms, foliage), xscr and xrot records. Each difference must be
  intended (detail slots, levels) or it is a fix that only lives in an overlay: port it into cp2gltf/cp77mat.
- Then the same check on four more tiles of different kinds (the ring, the hill, an interior-heavy tile, the shore).
- **One driver**, `tools/levelport/build_watson.sh` (or .py): extraction to kit in order, every step from committed
  tools, all settings in one place (--detail-slots 8, areas, tags): tiles, scene, merge, interiors, ocean, decals,
  effects, env, collision chunks, hub map, moon, kit. The patch_*.py and kitNN.py scripts become history.

### 3. Clean import, tag 11 (several hours unattended; full speed unless the owner games)

- New tiles tag 11 and scene `cp_watson_scene-11`; nothing rebuilt in place. Caches are reused only where their key
  holds the code that made them (cp2gltf's code_key); three tiles are also built cold, caches off, and must match
  byte for byte, which proves the caches hide nothing.
- Disk: a tag is about 38 GB plus 8 GB of packs; C: has 63 GB. Before starting, move superseded kits and scene builds
  (night-15 to night-24, scene 10 to 10i and their overlays) to .codex-trash with a manifest, keeping night-29 and 10j
  as the comparison.
- Output: kit `cp_watson_baseline-1` from the driver, with phase 1's binaries.

### 4. Verify (half a day)

- levelcheck on the kit; the parity check of phase 2 on all 268 tiles (counts and class totals against 10j +
  overlays, every difference explained).
- Looks: ref05 and ref09 at their solved cameras against CP2077 and night-29; the 55-camera spread against night-29,
  every changed view named. Foreground ground at Misty's must hold the r3ds result.
- Timing alone: the spread's frame times against night-29; load time, VRAM peak, 20 loads without a crash.
- An owner playtest of the kit.

### 5. Land it

- Commit `baseline/1`, retarget INTEGRATION.md and the handoffs to it, fast-forward the main line.
- With the owner's word: install it as Install 68, and fast-forward `master`.
- From then on every workstream branches from `baseline/1`, and a fix lands in the converter or the tree, never as an
  overlay on a kit.

## Shelved (2026-11-21 22:40)

Owner: no more Night City or xenjk development for now. Cleanup (owner's choices: kit only, all 2.31 data trashed, run
folders keep notes, the owner empties the trash): `<2.5 research root>/SHELVED.md` lists what stays and how to resume.
Kept: kit 15cam (and kit 15), every tag's inputs, scripts, logs and driver records. In trash, 327 GB when emptied: the
2.5 extraction and tiles, the 2.31 data and the D: archive. A resume is a fresh `--extract` build (about 5 hours).

## Resume here (2026-11-21 02:00)

**Merged (owner 2026-11-21):** `build/tag12-prep2` into master (63db73990): 2.5 is the documented process, and the guide
says it has not been replicated outside this machine (no clean-room run: owner, "no time to do more runs"). Before it
on master: the third-person camera fix (2638afe73: cg_cameraFPS over the predicted commandTime, cg_thirdPersonStepSmooth,
cg_thirdPersonVertSpring 50 ms, cg_thirdPersonHopFollow; owner "i like it").

**Play: kit 15cam** (`<2.5 research root>/levelport/build/cp_watson_baseline-15cam`): kit 15 with the camera-fix cgame,
everything else hard-linked from kit 15.

**Open, each needing a run (not started):** tag 16 (the ad-light fix in a kit: `inputs-16.json`, below); a clean-room
replication from a fresh clone and research root, following only docs/guide/night-city.md; then publishing.

**Earlier (2026-11-20 23:59). Play: kit 15** (`<2.5 research root>/levelport/build/cp_watson_baseline-15`, its Play_Watson_*.bat; CP2077 2.5 data,
master c6db3dc47 binaries; owner 2026-11-20 23:15 after the A/B: "Yes, switch to 15"). Its openjk.cfg took kit 12's
played lines (model cp_lucy_moon, g_moveStyle 2, ui_rankChange 7; the built file is openjk.cfg.built). Kit 12 stays
in place for the machinima session (its HANDOFF names it; told of kit 15). Kit 13 and 14 bulk is in C:'s trash.

**Tag 15 result (built 22:45, rc 0).** Watson from the installed CP2077 2.5 with every fix below, research root
`<2.5 research root>`, run by `resume_tag15.sh` (stage by stage after the stopped attempts; logs `tag15.out`/`.err`,
`tiles/15/driver/`). Collision recompiled all 214 tiles (chunk cache missed, see follow-ups), none failed. Verdict
against kit 12 (`tiles/15/driver/verdict/`), tag 14 in brackets:

| | tag 15 | (tag 14) |
|---|---|---|
| entity lights (scene.log; kit 12 23,534) | 23,981 | (37,630) |
| lights only in 15 / only in 12 | 22,248 / 21,213 | (35,897 / 21,213) |
| instances differing by mesh | 15,705 | (178,507) |
| instances only in 12 / only in 15 | 8,687 / 25,934 | (8,691 / 26,034) |
| materials only in 12 / only in 15 | 56 / 548 | (5,819 / 955) |
| ad screens only in 12 | 4 | (273) |
| engine.strict | 1,838 | (2,083) |

Both tag 14 fixes landed (entity light placeholders, hashed appearances). The 579,824 slot differences are a
renumbering; the 2,921 materials with other images are mostly ad screens showing another ad in 2.5 (sampled: the
rest are foliage and road textures within 1 level of 255 on average, re-encodes). Lights and instances only on one
side are 2.5 content (patch changes; tag 14's analysis below).

**A/B** (`<levelport root>/levelport/look/tag15-ab`: chain.sh, pairs.py, pairs_night/, pairs_day/; kit 12 reshot
beside kit 15 in the same session, same binaries, so every difference is data): most changed views ref05 23 %
(2.5 adds crates and pallets, drops a barrel at Misty's), diode_delamain_a 23 % and day_delamain 19 % (the Delamain
garage camera sits a few pixels lower: collision there changed), lamps_office 17 % (a lamp lit on the left, plant
placement), ref04_day/hilldn 10-17 % (foliage placement), signs 0-7 %. Nothing broken or missing in any view.

**Found after the switch: the ad lights.** Kit 15 colours 0 ad lights from their billboards where kit 12 colours
14,145 (scene.log `ad_light_coloured`, `ad_lights.json`): a mesh node's `lightsData` colour is black in 2.31 and
white from WolvenKit 10 (40 sectors sampled: 197 black to white, the one real colour unchanged), and cp2gltf colours
only the black placeholder. Fixed in `cp77_sector25.py` v4 (d616c5ad8, tested). The sectors stage converted only the
sectors it serialised in the same run, so a change to the rewrite never reached the JSON on disk; it now upgrades
every sector (95442474d). Kit 15 stays the play kit until tag 16 has it.

**Tag 16, ready, not launched** (heavy: waits for the owner's go; they were gaming): `<2.5 research root>/run_tag16.sh`
with `inputs-16.json` (tag 15's, `from_tag` 15, the same sea mask). `--extract` reruns the sectors stage (WolvenKit
unbundle; every sector JSON upgraded to v4, 6,628 files, 18 GB) and the scan; then tiles from tag 15 and whatever
parts the new placements change; collision should hit the chunk cache tag 15 filled. The dry run plans every stage,
93 GB to write with 134 GB free on C:. Check: scene.log `ad_light_coloured` near 14 k, then the A/B
(`look/tag15-ab`'s chain.sh and pairs.py, kit 16 against kit 15 and 12), the night views at the billboards first.

**Docs (62431202d):** the guide describes 2.5 through WolvenKit 10 as the process (order, inputs, timings, limits),
with `tools/levelport/areas/night_city_b/inputs.example.json`. The branch passes `publish_scrub.py`'s rules (0
findings on every changed file).

**Next:** tag 16 on the owner's go; the merge below on the owner's word (they want the 2.5 process official if
kit 15 plays well).

**Waiting on the owner:**
- merge `build/tag12-prep2` (worktree `.codex-research/wt-tag12`; master merged into it at 38defab00, so it is a
  fast-forward) into master; the owner said "lets do 2.5 before we merge". It edits none of the local-only docs.
- tag 12's engine.strict 43: contents-only ties at coincident brushes; the tie rule excuses flag ties, not contents.
- tag 12's other four misses are stale expectations (each explained below); expect-12 is not edited.

**Follow-ups found today (not done):**
- `memgate.py` (and jobguard) are in stage keys and in the chunk compile identity: editing the memory gate moved the
  parts stage key and missed every cached chunk. Infrastructure that does not change outputs should not key them.
- `cp2gltf chunks` peaks over 43 GB of commit on 32 GB of RAM (Watson); it now reserves 45 GB and runs alone, but it
  should not need the whole area's geometry in one process.
- 2.5 limits: WolvenKit 10.0.0-beta.1 does not export 11 meshes with textures; 11 instance lights white in 2.31 too
  are lost with the placeholder fix (Kabuki count; no field tells them apart).
- Portable checks: the checks stage needs a reference kit, which a first build by someone else does not have (the
  guide says `--until kit`); a check of a kit on its own (counts, spawns, a few engine traces) would serve them.
- Watson has no traffic signal programs (signal_instances 0 in every kit); `cp77_signals.py` was only run on Kabuki.

**Disk (21:40):** C: 129 GB free, D: 87 GB. C:'s trash: `20261120T212909-r25-tags13-14-kabuki-pilot` (about 247 GB
of tags 13, 14 and the Kabuki pilot; their logs and reports stay in `<2.5 research root>`) and a 3 KB stopped collision
folder. The 2.31 Watson bake cache and sector JSON are on D: (`<archive drive>/cp77-watson-231`, verified; the
C: paths are junctions to them).

## CP2077 2.5 (2026-11-20): tags 13 and 14, from the installed game, research root <2.5 research root>

Owner, 2026-11-20: future users have CP2077 2.5 or later; "lets do 2.5 before we merge". The 2.31 Watson bake cache
and sector JSON were moved to `<archive drive>/cp77-watson-231` (verified; the C: paths are junctions). Inputs
`<2.5 research root>/inputs-13.json`, `-14.json`; scripts `run_tag13.sh`, `run_tag14.sh`; reports
`levelport/cp77/watson/tiles/<tag>/driver/verdict/` (against kit 12, not judged).

What 2.5 (WolvenKit 10.0.0-beta.1) needed, each found by a run and fixed on this branch:
- Sectors: compiled node records and cooked nodes (inner nodes in a buffer, fixed-point instanced transforms,
  inline destructible transforms): `cp77_sector25.py` rewrites each sector in the 2.31 layout as it is serialised.
  Kabuki pilot: 65,600 placements against 2.31's 65,732, every instanced, destructible and static mesh at the same
  position; Watson: 5,313,290 placements against 5,293,416.
- Unset light colours read white (2.31: black, the placeholder the readers skip): put back black in sector instance
  data (11,842; 11 of Kabuki's 831 instance lights white in both versions are lost with them) and in entity
  templates and appearances (502 and 3,300, none white in both).
- Appearance names given only as their CName hash (FNV-1a 64) on 601,962 static and instanced placements: matched
  to the mesh's appearances (`MeshLods.app_name`).
- Not 2.5 but a fresh extraction: the entity fetch ran after the mesh fetches (about 92,000 entity component
  placements had no mesh in tag 13), and the device graph and `light_time.json` (night- and day-only lamps) were made
  by hand. `build_watson`'s extract stage now fetches entities first and makes both. Five tools did not start as
  `python -I` (the guide's way).
- Memory: the collision stage's chunk export (29.7 GB commit) ran ungated beside a game client and grew the page
  file 63 -> 87 GB: it now waits for its room, and the commit cap no longer follows a grown page file.

Tag 14 against kit 12 (before the light and appearance fixes): instances only in 12 92,212 (tag 13) -> 8,691;
lights whose type bits differ 6,466 -> 44; engine strict 12,147 -> 2,083. What is left looks like 2.5's own content
(the same meshes at other places: Arasaka waterfront walls, apartment interiors, door frames; the patch's new quests
and the Kabuki and Japantown vendor), slot indexes renumbered by a few new interior groups, and 11 meshes WolvenKit 10
beta does not export with textures. Mods: the extraction reads only archive/pc/content and ep1. Next: tag 15 with
the light and appearance fixes, then the owner's look at it.

## Tag 12 result (2026-11-20 07:30): built; verdict FAIL 23 of 28, every miss explained, two owner calls

Kit `build/cp_watson_baseline-12` (levelport/build, C:), run from wt-tag12 at 3ac64785..8ebcce13 (three restarts:
the memgate ledger rename race, the commit cap, the part reservations; each fix committed with a test). Parts gate:
268 of 268 collision parts equal tag 11's. Chunks 46 byte-equal + 149 brush-equal, 0 different; hub and spawns equal;
env and moon packs equal; materials: no unexpected change. Log `.codex-research/baseline-1/tag12-run4.out`; verdict
`levelport/cp77/watson/tiles/12/driver/verdict.txt` and `verdict/`.

The five misses (not "fixed" by editing expect-12; the owner decides):
- engine.strict 43: every one a contents-only difference (endpoint, fraction and surface flags equal; contents 0
  against 0x300000, both ways): coincident brushes in another order (tag 12 drops clutter in the chunk stage, 11c
  patched it after). The tie rule excuses flag ties, not contents ties: owner's call.
- instances 198 fewer and parity only_a 1,057 (expected 859): the 198 are CP2077 foliage litter (paper trash, cups,
  garbage bags) about 230 m under the area's floor, dropped by the review's RL-122 fix (cp77_foliage now applies the
  floor). Correct.
- meshes +165: +209 screen and ad meshes split by placeholder variant (the converter's stand-ins in place of 11za's
  patch pack), +1 the estate pond's water mesh, -45 their unsplit forms.
- detail cutouts 428, not 432: the 11za patch's 432 counted 4 materials that stayed opaque (patch_rep3b.log: 382
  opaque->cutout + 46 cutout->cutout + 4 opaque->opaque). 428 is the same set of cutouts.

Look changes the verdict does not gate, from converter code merged into master since tag 11 (never built into a
Watson kit before): 13 vehicle diode and fake-light materials emissive and untextured (11za: textured cutouts);
6 sign letters and lamps with an emissive colour; 8,959 materials' base images re-encoded (cutout mips weighted by
alpha, 0dca7f95). They need the owner's walk-through before kit 12 replaces 11zd.

Owner 2026-11-20 after the A/B (look/tag12-ab: pairs_night, pairs_day, zoom_ref05.png; a hybrid kit of tag 12's packs
on 11zd's binaries split the change: the data changes are small, most of the difference is master's renderer, crisper
neon and edges): "12 looks good". Kit 12 is the one to play. Prior Watson tags' bulk moved to
`<levelport root>/.codex-trash/20261120T075633-watson-prior-tags` (4,540 files, MANIFEST.tsv; about 75 GB returns
when the owner empties it): tile parts of tag 11 and the test tags, test kits and builds. Kept: kit 12 and its builds,
kit 11zd and every file it links (the castle and Night City video sessions use it; asked to switch to kit 12), the
2.31 extraction, logs and records. Still the owner's: the contents-tie rule (engine.strict 43).

## Tag 12 prep (2026-11-20): ready to launch, waiting on the owner's go

Branch `build/tag12-prep2` (worktree `.codex-research/wt-tag12`, from master e3f87daf), not merged:
- ffca3e33 RL-13: stage keys hash code the stages run without importing it (`STAGEKEY_FILES`: wk_read_lenient.ps1,
  blender_decimate.py, vcache.c).
- f9dacd5e Watson's env files and kit settings template in `tools/levelport/areas/night_city_b/` (byte for byte as
  11za's); build_watson takes them when the inputs leave `env`/`cfg` out, keys file inputs by contents, and fetches
  the moon under `--extract` (fetched from the installed 2.5: byte-identical to the kits' moon).
- 2b543981 RL-120: each bake file holds its code-and-sizes key and a mismatched one is baked again in place. The 39 GB
  bake cache is kept: the bake computation's code key is the same at all eight cp77mat.py commits since the lv2 bakes,
  so tag 12 reuses them (renaming them, the review's fix, would have re-baked all and doubled the cache).
- bf830908, b6c4d725 `toolchain.py` / `toolchain.json`: the outside tools pinned (q3map2 builds and libzstd by
  SHA-256; WolvenKit 10.0.0-beta.1, CP2077 2.5, PowerShell, Blender by version; Python modules); the preflight runs it.
- 9e364f23 the generators of signal_groups.json (`cp77_signals.py`), the ads (`cp77_ads_fetch.py`) and the sea mask
  (`cp77_seamask.py`), each reproducing the existing output exactly. Watson has no signal_groups.json (only Kabuki).

Inputs: `.codex-research/baseline-1/inputs-12.json` (backup `.bak-20261120`): no `env`/`cfg`/`moon` (repository and
extraction defaults, same bytes), no extra packs (GameData's Install 95 bike pack, with ct3x.bike), engine checks
and `--bin` = `.codex-research/baseline-1/bin-c6db3dc47` (master c6db3dc47, byte-identical to Install 95).
Checked: master's openjkded traces tag 11's collision identically (13 corpora, 2,865,342 traces, strict 0:
`look/tag12-prep/engine-master/engine.json`); five tiles' collision parts on master equal tag 11's array by array
(`look/tag12-prep/parts-p12`, tiles tag p12); the preflight passes (C: about 48 GB to write).

Launch (from the worktree; the env sourced and `XENJK_RESEARCH` set to the main checkout's .codex-research):

    python -I tools/levelport/build_watson.py 12 --data .codex-research/baseline-1/inputs-12.json \
      --bin .codex-research/baseline-1/bin-c6db3dc47

Expect a full rebuild (the review moved most stage and part keys) and memory-gated starts: the 5-tile check ran one
part at a time on the commit cap with other sessions' memory in use. Owner, 2026-11-20: future users have CP2077 2.5
or later; Watson's extraction is 2.31's and WolvenKit 9.0.1 cannot read 2.5's sectors, so a 2.5 extraction (10.0.0-beta.1)
is the replication run still to do.

## Update (2026-11-10 late): build loop items merged; review fixes; Install 77

- **baseline/1 head:** f0723fe0 = merge of buildproc/report4 9131b096 (owner: "Yes, commit and merge").
  - It carries REPORT4-fable's top eight. The review and the implementation record are under look/dev-speed-20261103:
    REPORT4-fable.md and r4/IMPLEMENTATION4.md.
    - D1: engine checks run on the packs' drive, with hard links only.
    - Z1: tile parts are deflated.
    - T1: a preflight checks every input before stage 1.
    - T2: the free-space gates check only the drives a stage writes, and stop at once.
    - T3: tile parts start on memgate reservations; D2 caps free commit at RAM + 16 GB.
    - T4: compiled chunks are reused by geometry key.
    - T6 + G1: the driver runs the clutter and spawn steps itself (areas/night_city_b_spawns.json).
    - V1: engine checks run on the dedicated server (engine_trace_ded.py).
  - D1's fix in look/collision-20261030/engine_trace.py and district_load.py lives outside the repo, and is live.
  - Not yet run: the full 268-tile scene_parity and a full clutter patch.
  - Before tag 12, .codex-research/baseline-1/inputs-11.json needs four fixes, which the preflight catches:
    - xenjk7 -> xenjk9 for the chunks;
    - three paths that pointed into the emptied trash: the night-29 reference kit, the scene-10j materials and
      night-29's SDL2.
  - tiles/11 still holds about 35 GB of uncompressed parts, not converted.
  - Scratch build/cp_watson_collision-t4b and -t6c are 5.3 GB each, not moved.
- **Character review:**
  - A Fable 5.2 independent check found F1-F12. Renderer fixed all of them and committed them by branch:
    - hc 2e66a569 and 3dd4b577;
    - vram e8f1c09d and 4f187955;
    - hr10 18664bd1;
    - sh21 72d5dede;
    - main checkout f6261534, b7ead0a9, 96c9b08f and 7b9b8285.
  - **Install 77** was the repaired packs: the three TX-7-damaged packs and the 23 re-made TX-15 packs. The
    **combined install is now 78.**
  - The owner's look decisions: HR-15's fade is on (0.2), TX-17 is on, and SH-8 is closed.
  - Remaining timed work:
    - in flight: TX-6, measured first, and the 6 GB VRAM runs;
    - folded into SH-23 with no timing: SH-20;
    - parked with reasons: SH-6/7 and CL-8.
  - At Install 78: drop TX-10's R_StreamHold on baseline/1 (F3). HR-10 now uses the G2_VERT_OFS_* offsets (F6).
- **Disk:** the owner emptied both trashes, so the Install 72/76 rollback copies are gone. D: is at 184 GB free.

## Resume here (2026-11-10 ~16:30, before a context compaction)

**Pick up here.** This session (Night City, the coordinator) coordinates Collision (local_d10492f0), Renderer
(local_1b203a4d), Gameplay (local_6f29f495) and Build Process Improvements (local_46960057). It builds the kits and
keeps this doc. Rules: commit only on the owner's word, push nothing, installs only on their OK. An install that
Renderer performs needs the owner's OK typed in Renderer's own session, because its permission check refuses
relayed approval.

**State: everything is idle.** Nothing is running and nothing is waiting on another session.

**Play: kit 12** (<levelport root>/levelport/build/cp_watson_baseline-12, its Play_Watson_*.bat; tag 12, master c6db3dc47 binaries; owner 2026-11-20 after the A/B: "12 looks good"). Before it: kit 11zd (11zc + the renderer of baseline/1 9dd47e72: terrain push off, teleport settle, cutout coverage on, 2026-11-16), kept in place while the video sessions use it. The kits table below (11n-11z) and
each kit's KIT.txt say what each adds. 11z = baseline/1 code through 87e37a5f + renderer R6b + the report packs
(rep3b, window env) + collision 11c (clutter) + the 120-spawn hub + cfg r_sceneRainTiles 0 and cm_chunkMegs 2048.

**baseline/1 head:** 58165c7d (merge of colclutter.py). Not merged:
- Renderer's Watson work on renderer/perf-2 (R1 rain tiles, R2 terrain push, the fx drag fix, the placeholder and
  cutout converters, R4 LOD crossfade, R6b window exposure), uncommitted in wt-vram-b1. The kits run its DLLs.
  Never put to the owner as a commit.
- Collision's spawn tool (cp_watson_spawns-11, put_spawns); it waits for the owner to play the spawns.
- Uncommitted docs: this file's 11x-11z rows; main checkout's docs/INTEGRATION.md (my edit: the combined install is
  77); other sessions' edits in INTEGRATION.md and renderer-vulkan-in-xenjk.md are theirs.

**Character rendering review** (docs/review/2026-11-09-character-rendering.md, main checkout; memory
char-review-coordination): the owner asked me to drive Renderer through it until fully resolved. Done except the
timed rows. Those are parked on the owner's word ("Not yet": too many sessions running); each row ends with a
"Timing run" line, so the runs can start straight from the table.
- Installed: Install 72 (TX-7 dedupe) and Install 76 (TX-20 BC1 maps, TX-15 layer DDS, SH-26 FMA mip fix; 103
  packs). TX-15 takes effect only with the char-fixes-vram renderer, i.e. the combined install (now 77).
- Branches, committed and unpushed:
  - claude/char-fixes-hc 2c249c32 then 4e4bc8d9 (SH-5b off), 883917d6 (r_fmaDebug);
  - claude/char-fixes-vram e7816d81 then 26cf8c3c (TX-15);
  - claude/sh21-material-set f1098abf, claude/hr10-hair-buffer a06fb1b5, claude/sk13-packed-vertex 80178677,
    claude/sh23-lit-shader 5b52d6d6, all on hc 2c249c32 and to be restacked at integration;
  - main checkout tools 2f3dd4df, 52d5250a (TX-20), 40d176ed (TX-15), 9d97c3f2 (SH-26); doc 864fb021.
- The owner's look decisions: every fix is on by default, except SH-5b FMA world shadows (off). SH-4 is on (Chloe's
  dress black).

**Watson open items:**
- Report 3 (plaza ground flicker) doesn't reproduce on any kit. It needs the owner's demo (`record plaza_flicker`)
  or a phone video.
- The tiled rain view (R1) has two bugs and stays off (r_sceneRainTiles 0).
- Renderer's perf list: sun cascades, occlusion, teleport settle, sort spikes, decals.
- Report 7's LOD crossfade is in (R4); CP2077's per-mesh LOD distances (xlod) stay off until an owner A/B. (Update 2026-11-19: nothing in rd-vulkan reads `.xlod` yet, so there is no switch to turn on; `tools/levelport/make_xlod.py` only writes `maps/<map>.xlod`.)

**Disk (2026-11-10):** C: 136 GB free, about 150 GB once its trash is emptied. The page file is 82.6 GB, and a
reboot shrinks it. D: 160 GB free, about 217 GB once emptied. In D:'s trash:
- 20261110T162211-charfixes-staged-copies, 28.3 GB;
- 20261110T162315-charfixes-stagebase, 28.4 GB: the only rollback copies of Installs 72/76, trashed on the owner's
  word.
C:'s trash holds 20261110T155319-cp77ref-run-renders-binaries, 14.3 GB. Engine-check run homes must sit on the same
drive as the packs (Collision's spawn checks copied 219 GB onto D:).

## Resume notes (2026-11-09 ~09:00, the previous block)

This session coordinates the other sessions (Collision local_d10492f0, Renderer local_1b203a4d,
Gameplay local_6f29f495, Build Process Improvements local_46960057), builds the kits, and keeps this doc. Commit only on
the owner's word; push nothing; installs only on their OK.

**baseline/1 commits since the last block:**
- 9a8bd1f8: merge collision/brushcook (cooker A, XBSZ chunks with zstd 1.5.7's decoder built in, the slab package,
  the memory-gated 4-worker driver, the cached hub). The owner's "if exe go ahead".
- 3891845c: docs.
- 3398e914: merge claude/r3-gameplay-fixes-212fe2 6c0645b0 (bike v3). The owner: "pull it in".
- ce60b563: merge renderer/fma-toon 28ee75a2 (the FMA Mobile game shading Install 68 shipped). shader_data.c takes each
  array from the side that changed it; the six frag_*_tx0_mask{,_fog} were regenerated. The renderer builds clean.
- 9b7943c8: merge renderer/vram-budget 657bd66f (one commit, the owner's choice: V3 VRAM budget, V4 count-then-scan
  cull, perf items 1-3, rebuilt on ce60b563 as B1). The merged source equals 657bd66f outside docs, and 657bd66f's diff
  from ce60b563 is line-for-line B1's source.diff, so kit 11s's renderer is this source.
- 461917a9: merge claude/r3-gameplay-fixes-212fe2 3b3dfbda (the owner: "Yes, commit and merge"): the bike climbs road
  grades, bike riders skip cg_smoothClients (rider on the seat at speed), the fork bone counter-rotates. The game
  modules for kit 11t are built from 461917a9, not from Gameplay's branch, so they carry baseline's other game code.
- 1302ebe3: renderer H2 (the owner: "Yes, commit both"), applied from look/perf-20261109/dll/H2/source.diff; the
  commit's diff equals source.diff, so kit 11t's renderer is baseline/1's rd-vulkan at this commit.
- 87e37a5f: merge claude/cpm-freestyle 5e79e68a (the owner: "Yes, commit and merge"; design "as designed": freestyle
  keeps JKA's acrobatics, style 1 is pure japro CPM). Game code equals 5e79e68a; kit 11u's modules are built from it.
- In progress: Collision is preparing the road-clutter rule (the owner: bags, piles, cardboard, tyres and pallets lose
  collision; bins, benches, chairs and barrels stay solid; road barrier stands and hanging clothes stay solid too;
  82,152 boxes; patched into cp_watson_collision-11c, in kit 11w). colclutter.py is uncommitted on
  collision/brushcook. Next from Collision: spawns across all of Watson plus one outside each key interior (CP2077
  fast-travel terminals and named entrances), as a hub-pack patch (put_spawns).

**Kits** (<levelport root>/levelport/build/), each one's KIT.txt and launcher rem line saying what changed:

| kit | what | state |
|---|---|---|
| 11n | 11m + XBSZ collision (5.69 GB, not 7.67) + the exe from 9a8bd1f8 | the owner played it; it crashed at the 9th hero model: VRAM |
| 11o | 11n + Renderer V3 (VRAM budget, fail-soft allocations) + cfg r_streamPoolMB 0 | smoke ok |
| 11p | 11o + V4 (count-then-scan cull, -963 MB at settle) | smoke ok, 6.85 GiB peak |
| 11q | 11p + Q123 (event-driven texture streaming, rotations on the GPU, 512-instance GPU group cull) | smoke ok |
| 11r | 11q + bike v3 game modules (from 3398e914) + home/base/zz_ct3x_bike_v3.pk3 | **the owner's launch got stuck** (below) |
| 11s | 11r + renderer B1 (Q123 rebuilt on ce60b563, so the FMA shading is in; look/perf-20261109/dll/B1, sha256 d071fc73...) | smoke ok (night views as 11r, 6.92 GiB peak); drops to the menu under VRAM pressure ("Image chunk limit") |
| 11t | 11s + renderer H2 (B1 + vk_image.cpp/vk_local.h: stb_dxt's MIN/MAX no longer leak, so over-budget images share 16 MB chunks; look/perf-20261109/dll/H2, sha256 bbd4ead9...) + cgame/jampgame from 461917a9 (bike fixes; baseline-1/mods-461917a9) | smoke ok (night views as 11s, 6.52 GiB peak) |
| 11u | 11t + cgame/jampgame from 87e37a5f (CPM + freestyle: g_moveStyle 1 CPM, 2 freestyle; baseline-1/mods-87e37a5f) | smoke ok with g_moveStyle 2 (loads, 5 views, 6.46 GiB peak) |
| 11v | 11u + renderer R3 (look/reports-20261109/dll/R3, f9c23130...: steam drag, terrain push; cfg r_sceneRainTiles 0 because the tiled rain view fails the diagonal moved-view check) + zz_cp_watson_scene_patch.pk3 from cp_watson_dev-rep3b (94bbdc92...: DEBUG placeholders 55, terrain flags 489, fence/grate cutouts 432) | report views against 11u: fences cut, DEBUG rocks gone; 6.24 GiB peak |
| 11w | 11v + colchunks packs of cp_watson_collision-11c (Collision's colclutter.py patch of 11z: 82,152 bags/piles/cardboard/tyres/pallets lose collision; bins, benches, chairs, barrels, barrier stands, hanging clothes stay solid; 4e078c4e... / 03c16791...) | Collision: verify ok, spawn gate as baseline-11, residency smoke pass, 240 clutter traces; my smoke: 48 chunks loaded, 5 views, 6.58 GiB |
| 11x | 11w + renderer R4 (look/reports-20261109/dll/R4, f317c816...: R3 + dithered LOD crossfade r_sceneLodFade 0.25; report 7's pops become ramps; drive cost within noise) | night views within 0.5-1.2 of 11w, 6.33 GiB; load 90 s because two other clients shared the machine (game lock slot 2) |
| 11y | 11x + zz_cp_watson.pk3 from cp_watson_spawns-11 (64edd57e...: 120 spawns instead of 19 - the owner's 3, 45 CP2077 entrances, 25 fast-travel terminals, 47 spread) + cfg cm_chunkMegs 2048 (owner's option C: preload capped at 2 GB, a spawn into a cold region holds ~7 s; the cap also bounds in-play chunk memory) | loads in 39 s, the cap logged, 5 views, 6.65 GiB |
| 11z | 11y + renderer R6b (look/reports-20261109/dll/R6b, e4390f93...: R4 + r_sceneWindowExposure as an xenv look value + parked probe-sky code, off) + zz_cp_watson_env_em.pk3 from look/reports-20261109/envpack (509efd69...: day xenv r_sceneWindowExposure 1, the owner's pick; dawn/night byte-identical) | day: report-2 window dark, 23 look values, no warnings; night views within 0.3-1.1 of 11x; 6.71 GiB |
| 11za | 11z + renderer P3 (look/reports-20261109/dll/P3, eec0ca3e...: renderer/perf-3 6ff583b7 = R6b committed as perf-2 46b6be7d + decal receiver normal, particle colour mask, r_sceneHideDither 0, r_sceneWaterBed 0, distant-water stand-ins not drawn) + zz_cp_watson_scene_patch.pk3 with stand-in materials 198/203/17984 flagged 0x2000 (b80c6d77...) + cgame camera fix (claude/camera-fps 56057110e, cg_cameraFPS 125; .codex-research/baseline-1/mods-cam-56057110, 0fd6e975...; jampgame/ui still 87e37a5f) | Renderer: water band/dots/shore shapes gone, graffiti wall whole, door dust squares gone, terrain push/LOD crossfade/window exposure unchanged vs R6b; my smoke: day, 24 shots, "3 stand-in materials not drawn", 7.04 GiB; with the camera cgame: loads, 24 shots, 6.99 GiB |
| 11zb | 11za + zz_cp_watson_scene_patch.pk3 from renderer/estate-water (065a33e3...: 11za's pack plus material 56230 water_mesh_arasaka_estate_a given the scene's default water record, flags 0 to 0x80, and its maps/cp_watson.xwat record); baseline/1 206d3603 merges the converter rule (a non-proxy global_water_patch.mt mesh with no water patch over it draws as scene water; scene only, collision unchanged). Owner 2026-11-15: "Yes, take it" | Renderer: 1 instance in Watson (tile 1_4, the estate garden pond and channels); +0.14-0.20 ms in the blend pass only while the pond is in view, nothing else changed; my smoke at the estate views: day, 4 shots, xwat 3 records, 6.35 GiB, the pond rippled water |
| 11zc | 11zb + renderer ER3 (look/reports-20261109/dll/ER3, 5d675a45...: baseline/1 f6ca2a12 + renderer/eye-relative e5e06180, merged as 87c9fa03; r_sceneEyeRelative 1, 0 = the old maths). The eye view projected through a viewProj carrying the whole eye translation; Watson reaches about 131,000 u from the origin, and float32 lost 5.4-7.6 u p99 of depth against 1.5 u layers (the plaza wall's backing quad 4 cm behind its panels). Now 0.22 u everywhere. Also carries the renderer fixes merged since P3 (perf-4, renderer tag12-prep). Owner 2026-11-16: "Yes, top four" | Renderer: plaza pan flicker per frame day 1,350 -> 443, dawn 946 -> 380, night 706 -> 197; wall wedges gone in every frame; cost within noise (4K MSAA 4); sweep (precision_sim.py) found every other path fine; Fable 5.2 review (REVIEW_OPEN_ISSUES_20261116.md): report 2 litter was precision (13 -> 0 collapses). My smoke at the estate views: day, 4 shots, 6.41 GiB |
| 11zd | 11zc + rd-vulkan of baseline/1 9dd47e72 (.codex-research/baseline-1/rd-9dd47e72, c9f5ae68...; build_x64, build_x64_zd.log): renderer/terrain-push-ab d5f0d46f (r_sceneTerrainPush 0: no measured benefit with eye-relative on, only see-through), renderer/settle-faces 42b989bb (r_sceneSettleFaces 150: a teleport settle redraws only the waiting light faces within 150 m), renderer/cutout-coverage 7ba02838 + 9dd47e72 (r_sceneCutoutCoverage 1 by default: cutouts covered per MSAA sample from alpha). From the Fable 5.2 open-issues review (look/reports-20261109/REVIEW_OPEN_ISSUES_20261116.md). Owner 2026-11-16: "Yes, all three" | Renderer: terrain push 5,549 of 101 M px over 49 views, fixed no fighting (terrain_push_ab.jpg); settle light shadows 1.27-2.67 s -> 0.00-0.04 s, faces within 150 m equal a full redraw right after a settle; cutouts: plaza pan slow-second flicker down 20-70%, +0.1-0.2 ms at 4K (cutout_coverage_ab.jpg). Scene SPIR-V regenerated byte-identical. My smoke at the estate views: day, 4 shots, 6.41 GiB, settles 1.59 s first then 0.02 / 0.00 s; launch 112 s (a new renderer's first launch builds its pipelines); **the one to play** |

**Open, in order:**
1. **11r stuck launch (2026-11-09 08:23).**
   - What happened: the window wasn't in the alt-tab list and the mouse was trapped. I killed pid 15664 on the owner's
     word. The log (11r home/base/qconsole.log) ends cleanly at spawn, 08:23:24, with no error.
   - Checked: the cfg's window and mouse cvars are the same as 11n's; there is no hide or skip-taskbar code; no test
     client overlapped.
   - The owner will retest. If they're stuck again: before killing it, read Get-Process .Responding and the CPU time
     twice. If it's hung, bisect 11n/11p/11q/11r (renderer Q123 or the bike modules); the owner must be away for a
     launch that takes the screen.
2. **Renderer:**
   - B1 (Q123 on ce60b563) passed the FMA check (Winry and Roy: 0.00-0.01/255 against ce60b563, at most 0.05 against
     Install 68), the look, vkmem and switch checks; it is in kit 11s. Record: look/perf-20261109/README.md "B1".
   - Committed as one commit (the owner: "One commit"), renderer/vram-budget 657bd66f, merged as 9b7943c8 (the owner:
     "yes put it in baseline"). Nothing pushed.
   - Next on its ranked list, under the owner's standing go: the decal pass (CPU), the rain-view redraws, then items
     4-7 (the eye sort's spikes, teleport settles 1.7 s, sun cascades, occlusion spikes). Look changes go to the owner
     as an A/B. Record: look/perf-20261109/README.md and look/vram-20261109/README.md.
   - Later: geometry pools and the light-shadow atlas sized to the budget, a low preset, then C (hero packs repacked
     stored DDS, roster_check) plus freeing unused models.
   - The owner's goal for later, not now: 1080p on a 6 GB laptop (memory watson-performance-goals).
   - Parked until asked: a turntable-clip character picker.
3. **Gameplay (idle):**
   - bike v3 is committed and merged; the owner hasn't ridden it yet (it's in 11r).
   - The owner's call: the float twin gate on the curb case, 0.053 against 0.05 u/s.
   - The owner said to leave the 95 newer skeletons riding in the swoop pose.
   - Engine sound: CP2077's traffic loop as 48 rpm loops (the player engine is a runtime plugin, ADM3, not decoded).
4. **Collision (idle):**
   - A+C is done and merged. The hub still uses q3map2's text path.
   - The door check (1,042 doors: 166 drawn closed, 127 invisible walls) resumes on the new path when the owner asks.
5. **The build process:** S1 is done (the stagger fix, 2288f674). S2, S3, S4, S6, Z1 and Z2 wait on the owner's go.
   The owner wants them before the next full tile rebuild (tag 12, with Z3 and per-sign text).
6. **Later, from the earlier list:**
   - the ink rasteriser (the street sign first, then the computer's main menu);
   - baseline/1 into the main line.
   - Install 68 and 69 were done by the models session, on the owner's word.

**Disk (2026-11-09):**
- C: 258 GB free, D: 226 GB.
- About 39 GB waits in <levelport root>/.codex-trash (20261109T042051-orphan-run-packs, 20261109T042232-tag10-scenes)
  for the owner to empty.
- Rules: our renders can go and cameras stay (memory screenshot-retention); character work stays but no duplicate
  copies; run retain.py from the main checkout.
- KEEP.txt lost its dead lines (gone hub_col, colchunks-3/4/5, col-4, scene-6/7/8, tiles/2, tiles/8, collision-tag5);
  col-2 stays for Renderer's occ runs.

## Resume notes (2026-11-07 ~12:00, the previous block)

**Pick up here.** This block is the state; the notes under "Resume notes" below are the detail.
- **baseline/1 at 60f1ca6e plus this doc**, nothing pushed or installed. Today's commits: 0878cc92 (no decals on loose
  props and foliage, the litter's depth pull: the report-2 flicker fix), 37520c19 (merge renderer/playtest-1107:
  reflections, water breakup, particle modules), c5446711 (merge renderer/water-fft: simulated FFT water), docs.
- **Kit to play: `<levelport root>/levelport/build/cp_watson_baseline-11m`** (11l + rd-vulkan_wfft). Kept kits:
  11 (tag 11 as built), 11b (the stutter fix; the collision session's checks), 11j, 11k, 11l, 11m. Checked on 11m: the
  report water, the night views, smoke; on 11l/11k: report 2's litter (0 paper collapses over a 240-frame orbit,
  daylight launcher; a second cluster too).
- **Disk:** the owner emptied both trashes after the cleanup (C: about 150 GB free) and solved D: themself.
  Traps found: retain.py only from the main checkout (a worktree copy reads no KEEP.txt); junctions in look/ (list
  reparse points before sizing or moving); research_shed.py unsafe on D: until fixed (walks the levelport junction,
  top-level KEEP names only). KEEP.txt's malformed lines from other sessions were repaired (`name  # note`).
- **Collision pivot (the collision session, local_d10492f0, is working):** the owner: "full pivot and stop wasting
  time on the doors". The door check is stopped and recorded (levelcheck-phase4-20261107/DOORS-README.md: 749 pass,
  166 drawn closed, 127 invisible walls; 77 of the first 100 are the door's thin jamb trim collided as a box). The
  chunks drop q3map2's text path: **cooker A** (q3map2's own brush code behind a binary reader, byte-identical on all
  195 chunks; owner "Option 1: keep format", no engine change), built as a library, about 1-1.5 days. Then a week of
  offline measurements for kit size (consult `.codex-research/collision-size-consult-20261107/`: Astra and Fable agree
  the best disk route is slab geometry on disk with exact brushes rebuilt at chunk load, about 2 GB, then disk-only
  instancing for 0.5-2 GB; the cost is load time). **The owner picks the size route from those numbers.**
  **2026-11-08, both reports in.**
  - Cooker A passed every gate on branch collision/brushcook (d8a0b00f, 077c5984, 1d3d07bf docs section 37):
    195 of 195 BSPs byte-identical, the import gate identical (spawns, smoke, 210-tile compare/drop, 5,120 walk
    lines).
  - Measurements (notes collision-size-measurements-20261108.md; data in .codex-research/collision-cook/measure/):
    - planes are 64% of the packs, and bevels are 6.72 of 12.16 sides;
    - the brush rebuild costs 15-35 µs a brush, so a dense chunk takes 1-1.5 min on one core (Fable said 1-3 s);
    - T fails the trace rule at 2-3x the kernel time;
    - all chunks placed measured 15.27 GB resident.
  - Projections, kit collision against 7.14 GiB today:
    - C (zstd -19, shuffled sides): 5.3-5.7 GiB;
    - B: 3.0-3.7 GiB;
    - A: 1.75-2.68 GiB.
  - B and A are byte-identical but cost load time. Only T would cut memory.
  - **Owner, 2026-11-08: "can we do a and c".** The package ships A's compact form (doubles, the tree rebuilt;
    about 1.75 GiB), expanded once at install by the cooker. The installed chunks are stored as C (zstd -19,
    shuffled sides; about 5.3-5.7 GiB; an engine reader). Sent to Collision in order:
    1. the S1 stagger fix;
    2. A's package and the expand tool (gate: 195 of 195 byte-identical);
    3. C's writer and reader (gate: decoded chunks byte-identical; load time; compression time).
    **Done and committed (owner's "Yes" in the collision session), on collision/brushcook:**
    - commits 2288f674 (S1), fea99686 (the engine: XBSZ chunks, zstd 1.5.7's decoder built in, lib/VENDORED.md),
      31fb28ed (XBSZ by default, the slab package colpack/colexpand, 4 workers, the hub cache) and 361ec42b
      (docs section 38); nothing pushed and not merged into baseline/1;
    - installed collision is 5.69 GB in place of 7.14, and the A package is 1.68 GiB;
    - every chunk decodes to cook-11's BSPs, 195 of 195;
    - spawn gate and smoke pass on the no-DLL kit cp_watson_zv-11; the walk is identical, and traces cost the
      same (the earlier +13% was noise from runs that were too short);
    - decode peak on -6_0 is 0.99 GiB;
    - the stage runs 4,850 s with 4 workers and the hub fresh.
    **Merged into baseline/1 2026-11-08 (owner: "if exe go ahead"):** merge 9a8bd1f8. The engine was rebuilt from
    it (build_x64, xenjk/openjk/openjkded.x86_64; no libzstd DLL). Kit **cp_watson_baseline-11n** = 11m with that exe
    and cp_watson_collision-11z's XBSZ chunk packs (2 packs, 5.69 GB, in place of 11m's 3 deflated ones). No tile
    rebuild: the owner wants the remaining build-process items (S2, S3, S4, S6, Z1, Z2) done before a full rebuild.
    The earlier plan, for the record. **Owner, after the plan ("Yes, that way"):**
    - C goes first: the cooker writes XBSZ chunks directly (zstd lumps inside the BSP, stored in the pk3, the
      engine decodes them on the worker); about 2-2.5 days;
    - then A, the package and colexpand.py, for moving or archiving a kit; about 1.5-2 days;
    - the package is dropped to .codex-trash/ after a verified expansion (195 of 195 byte-identical).
    Rule for C: no zstd long-range mode; the window
    stays at most -19's 8 MB (decode memory is the window, which matters on a 1-4 GB server); a lower level
    if compression is too slow. Its report gives decode memory, decode time against inflate, and trace time.
  - **S1 done 2026-11-08, not committed** (cp77_collision.py +84/-29 on collision/brushcook):
    - starts wait only for memory: a peak estimated from the glb size, plus 2 GB;
    - the rerun cp_watson_collision-11s1 took 4,540 s (1.26 h) against 10,891 s, 220 worker-minutes, 3 workers;
    - its 197 pack members are the same size and CRC as 11cook's (I checked);
    - what's left: the tile work itself, plus the hub's -light holding the CPU for its first 44 min;
    - a 4th worker is unmeasured (11 GB was free at worst). Owner: "Yes, commit" (sent to Collision to commit).
- **Build-process (Build Process Improvements, local_46960057, idle):** S2, S3, S4, S6, Z1, Z2 wait on the owner's go
  (Z2 and Z4 come free with cooker A); S5 is moot on the cooker path. S1 is not (2026-11-08): the cooked kit run's
  3.0 h was cp77_collision.py's fixed 45 s start stagger (198 of 213 gaps; 209 worker-minutes of work, about 70 min on
  3 workers). Sent to Collision with the owner's yes, for after its measurement runs. Z3 goes into tag 12 (the no-decal and
  on-surface tagging in the interiors pass). Breakdown given to the owner: after S1-S4 about 3.1-3.4 h, collision
  compile about 60% of it (now superseded by the cooker).
- **Renderer (local_1b203a4d, idle):** both its branches are merged. Open: the sun shadow's stair-steps on the water
  (cascade resolution, deferred); the steam column needs a CP2077 capture; dll/P6, dll/W3 and fx-p5 can be retired.
- **Open for the owner, in order:** the collision reports (cooker A, then measurements -> the size route); the
  build-process go; the ink rasteriser (street sign first, then the computer's main menu); the rebuild decision (tag
  12 with Z3 and the per-sign text); a playtest of 11m; Install 68; baseline/1 into the main line.

## Resume notes (2026-11-07 ~07:30 to ~12:00, the detail)

- **Committed 0878cc92 (the owner's yes, 2026-11-07):** the no-decals and on-surface instance bits
  (tools/levelport/tag_interiors.py: bit 25 on destructible, physics, dynamic, rotating, cloth, entity and foliage
  instances; bit 26 on foliage), scene.vert (passes bit 25, pulls bit-26 instances toward the eye along their ray, depth
  only; the signal slot masked to bits 0-23), scene.frag (no DBuffer decals on bit 25), the regenerated SPIR-V, and
  this doc. Committed earlier today: 00f68dd9, ae8cda0a, 26ed6f7d, af0eba0d and the docs commits up to c796bda8.
- **Kits** (`<levelport root>/levelport/build/`): `cp_watson_baseline-11m` is the one to play (11l plus the simulated water, below);
  `cp_watson_baseline-11l` (11k plus the merged renderer and FX pack, below); `cp_watson_baseline-11k` (11j plus the bit-26 pull:
  fixes the report-2 flicker, verified at two litter clusters); 11j (11h plus no decals on loose props and foliage); 11b
  (the stutter fix, the collision session's level checks); 11 (tag 11 as built). 11d-11h (debug views 18-20, ad
  screens, sign gradients, rain view, diode lamps: all carried into 11j) were cleared out with the 2026-11-07 cleanup
  and the owner emptied the trash. Renderer DLLs of this round in
  `.codex-research/baseline-1/rd-vulkan_{rain1,rain2,rain3,dbg1,dbg2,nd1,os1}_x86_64.dll` (os1 = the tree now). Scene
  builds: cp_watson_scene-11-int-nd2 (11j), -int-nd3 (11k).
- **Disk (2026-11-07 cleanup, trash emptied):** C: 152 GB free, D: 78 GB. Run tools/levelport/retain.py from the main
  checkout only (a worktree copy reads no KEEP.txt); list junctions before sizing or moving a folder (look/p1b-p2-p10's
  tree*/game_directory is the live D: game folder). research_shed.py on D: is unsafe until fixed (it walks the
  levelport junction and honours only top-level KEEP names).
- **Report 2 flicker: fixed in kit 11k (the bit-26 pull), verified 2026-11-07 ~08:45.** The owner's frame analysis
  (paper sheets lose most of their area for a frame and come back: frames 139-140, 147-149, 186-189 of
  r2_before_after.mp4, both panels) named it. The litter is CP2077 foliage (paper_trash_a/l, newspaper_b_full,
  trash_packaging_c) lying flush with the road: road and sheet depths tie, and whole flat sheets lose for a frame. Over
  the 240-frame orbit, frames where the white-paper area dips >8% below both neighbours: 11h 29, 11j 26, 11k 0
  (scratchpad paperdips.py: low-saturation bright pixels in the ground half). A second cluster ~1 km away
  (foliage24642, 1,322 paper_trash_a; cams_litter2.txt, runs/litter2_11j and _11k): 11j 2 collapses (frames 178,
  203: scraps cut away), 11k 0 (frames 126-131 dip on both: the camera passes behind a wall). The pull is applied
  only in the eye's views (pc.eye.w), not in local-light shadow maps. My earlier "11k does not visibly change it" was
  wrong: I judged without these frames. So 11k is the kit to play. Superseded notes on the hunt: The owner: "both sides of that video has the
  flickering ... its right there in the video". Video: `look/cp77ref-20261104/runs/r2_before_after.mp4` (8 s, 30 fps;
  left kit 11h, right 11j; frames in runs/r2orbit and runs/r2orbit_11j, camera path cams_r2orbit.txt: 240 frames
  circling (3976, -9183, -3.6) at head height, 1.6-6 m). Not it: the decals on the litter (fixed, 11j), a temporal
  flicker at a still camera (12 lossless shots, 0 pixels changed), sub-pixel pose shifts (fine grain only), per-frame
  sharpness (smooth), Codex gpt-6-astra on 16 frames (`.codex-research/codex-r2flicker/CODEX_ANSWER.md`: no pop, no
  surface swap; suggests fine-detail shimmer). Asked the owner for the second and the place on screen; go straight
  to those frames. The litter there is CP2077 foliage (instances named foliage<node>_...#n, placed flush with the
  road at z 11.87).
- **Street signs and computers** (owner: street names on the signs, CP2077's default computer screen): findings below
  (TweakDB records, the inkwidget, the inkstyle colours, Blender Pro OTFs in look/streetsign-20261107/). Needs an ink
  widget rasteriser. Per-sign text: a scene rebuild (tag 12) or a renderer atlas feature; the owner: "lets see what
  else comes up before we go either way". Computers: one image, patchable.
- **Other sessions:** Renderer (local_1b203a4d) has reports 1, 3, 7, 8, 10 (smoke, water tiling, reflections, blocky
  puddle reflections). Collision (local_d10492f0) has the level checks (resumed), the Misty's awning and the glass you
  fall through. Build Process Improvements (local_46960057): REPORT3 received, checked against baseline/1 at
  00cca71a (`<levelport root>/levelport/look/dev-speed-20261103/REPORT3.md`): an import 5.5 h -> about 3.5 h with
  S1-S4, 79 GB an import -> about 43 GB with Z1-Z3. Nothing implemented.
- **Water merge: done 2026-11-07 (the owner: "we will take it and merge").** The owner on 11l's water: "it still looks
  like repeated/tiled images" (one baked ripple frame slid in copies; CP2077 simulates its water, water_patch_fft.dtex).
  Merge c5446711 of the renderer session's renderer/water-fft e65a3a4d (no conflict; the merged tree equals the
  branch's; the shader data regenerated identical): three FFT wave cascades simulated every frame, r_sceneWaterWaves 0
  restores the ripple image; also Scene_SetLayout's binding array 24 -> 32 (written past its end). The renderer
  session timed it below run noise (waves on 5.3-5.7 ms, off 5.6-6.1, the report view). Renderer
  `.codex-research/baseline-1/rd-vulkan_wfft_x86_64.dll`; kit **`cp_watson_baseline-11m`** = 11l + that renderer.
  Checked on 11m (runs/k11m_day, k11m_night; cams_k11m_*.txt): the report water at 1:1 wind-aligned waves of mixed
  sizes, no repeated highlights, changing second to second; the night views and smoke within 0.3-1.4 of 255 of 11l's.
  The sun shadow's stair-steps on the water stay open (cascade resolution, deferred).
- **Renderer merge: done 2026-11-07 ~09:05 (the owner's "yes, merge it").** Merge 37520c19 (scene_shader_data.c
  regenerated, the bit 25/26 edits kept); renderer `.codex-research/baseline-1/rd-vulkan_m1107_x86_64.dll` (sha256
  48d72973...); kit **`cp_watson_baseline-11l`** = 11k + that renderer + fx-p5's zz_cp_watson_fx.pk3 (both swaps
  checked by cmp; r_sceneSsrFlat 0.95 and r_sceneWaterBreakup 1 are the renderer's defaults). Checked on 11l
  (runs/k11l_night, k11l_day, r2orbit_11l; cams_k11l_*.txt): the wet street (fix on and off), the weird-reflection
  view, ref05 and ref07 within 0.2-1.1 of 255 of the renderer session's own shots of its branch; the water's grid gone
  with the breakup on and back with it off; the smoke view draws; report 2's orbit (Day launcher, as before) 0 paper
  collapses, the same as 11k (1.561% paper). The orbit with the night launcher is runs/r2orbit_11l_night (the paper
  is dark there, so the count doesn't apply). Not run: an in-game look at the particles' new motion (the steam column
  has none of the modules; needs a CP2077 capture). The renderer session's notes on the branch follow.
- **Renderer merge (the owner, via the renderer session: add to the sequence):** branch `renderer/playtest-1107`
  (worktree .codex-research/wt-pt1107), two commits on c796bda8, not merged or installed: 267820fd (r_sceneSsrFlat
  0.95: the puddle stair-steps and the neon dashes; r_sceneWaterBreakup 1: water's middle-distance grid; report 2's
  plate is grime decals plus moon shadows, not the renderer) and d71daaa8 (cp77fx.py carries VelocityTurbulize, Noise,
  SizeByDistance; scene_fx.vert draws them; the steam column has none of them, so it needs a CP2077 capture next).
  Notes: look/cp77ref-20261104/README.md "The owner's 2026-11-07 Watson playtest", sheets/pt/.
  1. After this round is committed: merge the branch into baseline/1. scene.frag/scene.vert touch other regions (the
     renderer's word; check at the merge); scene_shader_data.c conflicts: regenerate with tools/scene_spirv.py.
  2. Build the merged renderer. The renderer's dll/P6 (2106cdca) is its branch tip without bits 25/26, so on kit 11k it
     draws decals on the litter again: P6 is not the build to ship.
  3. A new kit: 11k plus the merged renderer plus the FX pack built by the patched converter (tested copy:
     look/stream-20261104/fx-p5/zz_cp_watson_fx.pk3; every old field byte-identical).
  4. Check the renderer's views (sheets/pt) and report 2's litter (no decals) on it.
- **Build-process sequence (the owner: add it to the sequence, don't execute yet):**
  1. Done: this round is committed (0878cc92; tag_interiors.py shares a file with Z3).
  2. Build Process Improvements implements on baseline/1: S1 (compile stagger by size; cp77_collision.py is the
     collision session's, its agreement first), S2 (part start gap by size), S3 (tiles.key, no second tiling), S4
     (part_jobs keyed by stat), S6 (driver job guard), Z1 (tile parts deflated level 1: every reader is a plain
     np.load, none memory-maps; gate on equal arrays and byte-identical kit packs, not identical part files), Z2 (clear
     the q3map2 scratch once the packs check). Gate: tag 11's outputs on a three-tile tiling.
  3. Z3 at tag 12: the no-decal and on-surface tagging moves into the interiors pass, so the geometry is written once
     (no -int-nd copies).
  - **On hold (owner, 2026-11-07: "i feels like we are hanging on to BSP needlessly"):** S1 and S5 only speed up the
    q3map2 compile. Owner, the same day: "full pivot and stop wasting time on the doors for now": the collision
    session stops the door check (1,042 doors: 749 pass, 166 drawn closed, 127 invisible walls; recorded to resume
    on the new path) and replaces q3map2 for the chunks. Its scope (shared notes,
    collision-without-q3map2-scope-20261107.md): the chunks already use colmerge's own kd-tree (q3map2's is dropped);
    q3map2 only cooks each brush (planes with exact-planes/pointgrid, box and edge bevels, degenerate rejection,
    contents, order). Plan (owner: "Option 1: keep format", over an own brushes+BVH file with a cm_* loader): a numpy
    brush cooker writing those lumps from the slab arrays, colmerge's tree and writer unchanged, no cm_* change, any
    OpenJK. Gate: planes, sides and brushes byte-identical to today's q3map2 output on
    all 195 chunks (traces then identical, tie flags included), the 10-31 rule as fallback, then the spawn gate,
    compare/drop, smoke, the settled walk. About 2-4 days; -6_0 577 s -> about 100-200 s; the stage 310 -> 60-100
    worker-minutes; data unchanged (7.1 GB: the planes and sides are the brushes). The hub keeps q3map2. S1 and S5
    are moot for chunks. Cooker options (2026-11-07): A, q3map2's own brush code behind a binary reader (exact,
    1-1.5 days), over B (bit-exact numpy port) and C (planes from slabs, trace-gated). None changes the kit's size.
    Size consult (owner: "ask astra high and fable for options"; `.codex-research/collision-size-consult-20261107/`):
    both agree 7.1 GB is this representation's cost (258 B a stored brush against 12-19 B a triangle; render-grade
    input; no reuse on disk). The best disk route is compact slab geometry on disk with today's exact brushes rebuilt
    at chunk load by the same q3map2 brush code, about 2 GB and byte-identical, plus disk-only instancing for about
    0.5-2 GB; the cost is chunk load time (1-3 s a chunk estimated). Fable's memory option (planes derived at the trace)
    halves memory but only meets the trace rule. Owner: "ok go": cooker A first, then a week of offline measurements
    (per-lump compression, the bevel share, rebuild time a chunk, snap flips, the slab kernel, resident bytes); the owner
    then picks the size route. Door finding so far: 77 of 100 invisible walls checked are the door's thin jamb trim
    (solo_frame, door_sliding_*_nonsolo_blend) collided as a box at the opening's edge.
  4. After tag 12: S5 (compiled-chunk cache keyed on the glb, gltf2map/q3map2 code and binary, args; its own
     three-tile cold check; it makes the collision session's awning and glass fixes minutes, not a 3.6 h stage); S7
     (profile a one-tile look build on a free machine).
  - Skipped: Z5 (the extraction is frozen at 2.31; every reader would change). The owner's call: Z6 (6 GB of tag 4-9
    placement snapshots to the trash; nothing current reads them). The collision session's call: S8.
- **Test-harness facts:** run_kit.sh refuses while any client runs (other sessions' too): wait for it; a cam line's
  post commands run after `benchcam off`; a screenshot needs two frames (back-to-back requests drop every other one);
  debug views in the cam's own commands, view 8 survives JPEG; 15, 16, 20 need PNG.
- **Next, in order:** (the renderer and water merges are done: kit 11m is the one to play); the build-process steps 1-2 above (on the owner's go); the ink rasteriser (street sign first, then the computer's main menu); the
  rebuild decision (tag 12 with Z3); the owner's playtest; Install 68; then baseline/1 into the main line.

## Earlier resume notes (2026-11-07 ~04:45)

**Owner, 2026-11-07:** "agreed to all and yes when ready merge into main line": the order below, and `baseline/1` merged
into the main line once Install 68 passes the owner's playtest.

**State.** `baseline/1` (worktree `.codex-research/wt-baseline`) holds every merge (phases 0-1), the p3-carry renderer
features (all default off), the driver `tools/levelport/build_watson.py`, the collision tool `cp77_collision.py`, the
ocean in the converter, 8 detail slots by default in the converter, the movement-stutter fix 53551ce8, and (committed
on the owner's word, 00f68dd9 and ae8cda0a) the rain view following the eye, r_sceneDetailSlots 8, the device_diode
lamps and `tools/levelport/patch_materials.py` (below). Nothing is pushed or installed. Binaries:
`.codex-research/baseline-1/build_{Win32,x64}/Release` (build.sh, -nodeReuse:false); renderer builds of this round kept
as `.codex-research/baseline-1/rd-vulkan_rain{1,2,3}_x86_64.dll` (3 is the current tree's).

**Kits.** `<levelport root>/levelport/build/cp_watson_baseline-11j` is the one to play (11h plus no decals on loose props and foliage; 11h: 11f plus the placeholder ad screens and the debug views, below; 11f: 11e plus the sign gradients). 11e: 11b (tag 11, the stutter fix)
with the patched materials pack and the current renderer. 11d is the same with rain1 (the finest-LOD rain draw, 13.6 ms
a redraw: do not play). `cp_watson_baseline-11` stays for the level checks. A deflated first try, 11c, is in
`<levelport root>/.codex-trash/20261107-kit11c-deflated` for the owner to empty.

**Level checks: resumed** on the owner's word (the 13 suspended processes, psutil resume, 04:00); the collision session
(local_d10492f0) was told, and also given the owner's Misty's awning report (-10454 -39167 185) to work as a class.

**Done this round (El Coyote, the owner's CP2077 shot at 01:40; evidence `<levelport root>/levelport/look/cp77ref-20261104/runs/`
coyote_11d_sheet.jpg, coyote_r1_r3.jpg, ref05_old_r3.jpg, rainmove_1 and rainmove_3):**
- Wet floors indoors, a class: the rain view was one 437 m square placed at load on the centre of the lights, and
  Watson's lights span 3.5 x 4.9 km (4% of them inside it; El Coyote 2.3 km out), so nearly every covered floor in the
  city read wet. It also left out interiors' geometry (the cull's interior test) and never redrew for streamed
  geometry. Now (vk_scene.cpp Scene_RainFrame, r_sceneRainFollow 1): on a map wider than the view it follows the eye,
  drawn again in the frame's commands once the eye is a quarter of it from its centre or once streamed geometry came in
  (at most once a second), interiors included (scene_cull.comp counts.w bit 3), at the eye's LODs and nothing under
  half a metre. Measured moving through the city at the owner's 4K settings: 2.8 ms of GPU a redraw (13.6 ms at the
  finest LODs: frames to 30 ms, rejected), no frame over 25 ms, p99 as with the follow off. El Coyote dry; ref05's
  road stays wet, the pavement under Misty's awning now dry.
- The pool-table lamp, a class: device_diode.mt names its colour EmissiveColor1 and has an on/off EmissiveInitialState,
  so the converter's glow test never fired: all 244 such materials of Watson's meshes (126 in the scene, 79 lit) drew
  opaque and unlit (corridor and bar lamps, LED strips). cp77mat.py reads EmissiveColor1 while the state is on. The
  kits carry it through `patch_materials.py` (re-converts a template's materials, rewrites their records in
  materials.xscn, a stored `zz_cp_watson_scene_patch.pk3`: a deflated one turns geometry streaming off, 4 GB more VRAM
  and a dropped map); a rebuild carries it from the converter. 2 microwave sticker materials wait for a rebuild.
- r_sceneDetailSlots defaults to 8 (the owner's choice).
- Found, not changed: the lamp's lights and channel boxes were right; every El Coyote mesh is placed (1,807 of 2,357
  nodes within 12 m: the misses are clutter). The green strip over the back counter (corridor_light_h q000_el_coyote,
  EmissiveEV 0.4) and the El Coyote logo sign (signages.mt, EV 1.1) convert right but come out at 0.1 on our "EV 5 =
  1" scale, where CP2077 shows them bright inside the dark bar: likely CP2077's interior exposure, a look-model
  question for the renderer, not a converter knob. The two TVs ({sq022_dvc_el_coyote_tv_}, tv_1.ent) have no instance
  data: whether they are on is the device's default or quest sq022's, not in the sector; the owner's call. The ring
  light colour is not looked at yet.

**Ring light (2026-11-07, later):** signages.mt with UniformColor 0 runs ColorOneStart to ColorOneEnd across the sign;
the converter used the start colour alone (155 of the scene's 488 sign materials; El Coyote's Kiroshi ring red where
CP2077's is magenta-white). cp77mat bakes the gradient (along U, or V; GradientStartPosition, ColorGradientScale) into
the mask, which then glows white; vertex colours are zero on every sampled sign, so the map is the gradient. Kit
`cp_watson_baseline-11f` (11e plus 154 patched sign records, 152 new images; patch_materials.py now carries new images
and an earlier patch pack's): the ring pink-white as CP2077's; ref05 (Misty's) unchanged (0.14% of pixels, steam and
reflections). Its check: all 333 single-colour signs came back unchanged, so the patch tool names images as the build.

**Triage of the owner's other reports (2026-11-07):** glass you fall through beside black frames you stand on (5):
collision, sent to the collision session with the Misty's awning. Street sign (6, {KennedyAve} street_signs_panel
mesh_ui) and computer (11, {sq018_dvc_computer} monitor_device_d_screen_a): runtime-UI screens (metal_base_ui) get a
stand-in ad from the pool; CP2077 draws a street name and its default computer screen: what they show instead is the
owner's call (dark, a generic UI glow, or rendering CP2077's own UI). Still to look at: z-fighting (2 debris), mid-distance water tiling (3), low-res street (10), smoke puffs (1), reflections (7, 8); the city
view (9) shows no fault to us: ask.

**Report 4 ("zfighting", the pink strip by the highway, -10842 -85253 280 yaw 78.7; runs zf_11f .. zf_11h in
look/cp77ref-20261104/runs/):** not z-fighting: the Corpo Plaza roundabout's long ad screen (cct_cpz_roundabout_a_
commercial_screen, television_ad.mt) showing its placeholder AdTexture, color_checker_gradient.xbm (sixteen colour
tiles, red to violet, stretched over it). 29 of the scene's 57 television_ad screens have it (billboards mostly); CP2077
fills them with ads at run time. cp77mat gives them an ad as the UI screens get theirs (CP2077's own via cp77_ads, else
one of the area's; only the checker and the editor's textures count as placeholders: the metro line signs, LED
flipbooks and gradient signs are content). Kit `cp_watson_baseline-11h` (11f plus 29 screens, one ad each: a rebuild
varies them per placement; the other 28 came back unchanged). It took long because scene.frag returns early for a
glowing surface, before the debug views: r_sceneDebug 15/16 named the wall behind it, 1/18/19 showed the screen's own
colour. The renderer now answers 15, 16 and 20 for glowing surfaces too, and has 18 (albedo UV), 19 (albedo texel), 20
(albedo image index) and the `sceneimage` command (an index's image, or `m <material>`'s images).

**Street signs and computers (owner, 2026-11-07: "street sign needs to show the street name ... computer screens
should show that default cp2077 view"):** both are CP2077 ink widgets drawn at run time onto a metal_base_ui panel.
Found so far (evidence `<levelport root>/levelport/look/streetsign-20261107/`):
- A sign entity's instance data holds StreetSignWidgetComponent records: `signSelector` StreetNameSelector
  `recordID` (e.g. NorthsideIndustrial.KennedyAve; isAStreetName 1) and a numeric `streetSignTDBID` (isAStreetName 0).
  TweakDB (tools/bike/tweakdb.py, tweakdb_ep1.bin) gives `streetName` Kennedy Ave, `districtName` Watson,
  `subDistrictName` Northside Industrial, `styleStateName` northside_industrial and `resource` = the widget,
  base/gameplay/gui/world/street_signs/generic_street_name/generic_street_name_sign.inkwidget (660 x 200: the name in
  Blender Bold 68, a footer bar with sub-district and district in 34, gradients) with
  generic_street_name_sign_style.inkstyle (per district: StreetNameColour, BackgroundColour, FooterColour, gradients).
  CP2077's fonts are in the .fnt resources as raw OTF (fontBuffer): blenderpro-bold.otf, blenderpro-book.otf there.
- Watson has ~5,900 sign panels: 4,122 street_signs_panel_3_3x1 (street names), ~1,500 1x1 (other sign widgets).
  One material serves them all, so per-sign text needs per-placement variants (the stand-in ads' mechanism: new
  meshes, a scene rebuild) or a renderer feature (an atlas frame per instance, patchable).
- Computers ({sq018_dvc_computer}: computer_1.ent, ComputerControllerPS ComputerSetup): startingMenu MAIN, mails, files,
  internet and system menus on, stylePreset ComputerUIPresets.DefaultPreset (TweakDB: its button, mail and file
  widgets, DevicesUIDefinitions.*). Every default computer shows the same main menu: one image, patchable per material.
- Both need the widgets drawn: a small ink rasteriser (canvas anchors and margins, flex panels, text in CP2077's
  fonts, rectangles, gradients, atlas images), the street sign first (the simplest), then the computer's main menu.
- A scene rebuild costs: parts ~80 min, scene ~4 min (tag 11's logs); a new tag also redoes tiles (~10 min) and the
  collision (~5.5 h last time) unless it is reused.

**Remaining reports, sorted (2026-11-07; owner: "lets see what else comes up before we go either way" on the
street-name delivery):**
- Report 2, debris "zfighting" (3868 -9178 134, day): seen in a camera orbit at head height (runs/r2orbit, video
  r2_orbit.mp4): the graffiti and paper decals project onto the debris boxes (trash_packaging, destructible meshes)
  and slide over them as the eye moves. CP2077's world decals mark the static street and walls, not loose props. Fix:
  tag_interiors.py sets instance slot bit 25 on destructible, physics, dynamic, rotating, cloth and entity instances
  and foliage (the debris at the spot was CP2077's .cfoliage litter, placed_name 'foliage...': no tile record, found by
  the instance index under it); 2,598,543 instances in Watson. scene.vert passes it, scene.frag skips the DBuffer there
  (the signal slot masked to bits 0-23). Scene build `cp_watson_scene-11-int-nd2` (identical to -int but bit 25), kit
  `cp_watson_baseline-11j`; verified at the spot and in a before/after orbit (runs/r2_before_after.mp4). The first try
  without foliage (11i, -int-nd) is in <levelport root>/.codex-trash/20261107-kit11i-decals-missed-foliage.
  Owner on the before/after video: "both sides of that video has the flickering": the decals on the litter were real but
  not the flicker. Since: a still camera is stable (12 shots, alternating frame parity: 0 pixels changed); 0.02-unit pose
  shifts change only fine grain (graffiti and litter alike, both kits); the foliage litter is authored flush with the
  street (z 11.87, the road's), so kit `cp_watson_baseline-11k` also pulls foliage toward the eye along its ray (instance
  slot bit 26, scene.vert; scene build -int-nd3): not shown to fix the flicker. Waiting on the owner to say what flickers
  (which element, where in the video).
- Report 10, "street still low-res": the asphalt is fine; the puddles' reflections are blocky with horizontal steps
  (screen-space reflections traced at half size). Renderer (a new renderer only).
- Reports 3 (water tiling mid-distance), 7, 8 (odd reflections), 10 and 1 (smoke): sent to the renderer session
  (local_1b203a4d) with their views, 2026-11-07.
- Report 1 (smoke as a line of regular puffs): the particles converter or the FX simulation (an .xfx patch or a
  renderer change).
- Street names: per-sign text needs a rebuild or a renderer atlas feature (above); computers: one image, patchable.
- Test harness note: a cam line's post commands run after `benchcam off`, so a screenshot taken there is the
  player's own view (pitch 0): take debug views with the cvar in the cam's own commands (r_sceneDebug 8 survives
  JPEG).

**Owner's other playtest reports, still to work as classes** (evidence `.codex-research/baseline-1/playtest-20261107/`
and `playtest-20261107b/`): smoke rising as a straight line of regular puffs; z-fighting twice (one on debris); water
good near, tiled mid-distance, needing a blend to far; falling through glass beside black you can stand on; a street
sign showing something else; two odd reflections (left of image); a city view; a street still low-res; a computer not
showing its default screen. Also offered: a settings-cost study at 4K (after the playtest).

**Next:** the other reports as classes (triage above); the level checks'
results from the collision session; then the owner's playtest (kit 11e or a later one), Install 68 on the owner's word,
and `baseline/1` into the main line.

## Phase 0 result (2026-11-06)

Committed as they stood, on the owner's word ("Yes, start phase 0"):

- `renderer/pbr-materials` a6d78961 (the level port: nit lights, multilayer levels, cp77decals.py, this plan) and
  42b077ce (the wishlist docs and the Install 55/62/66 notes the other sessions left uncommitted).
- `wip/stream`: H14-H44 as 31 commits on d09e5561, one a build, each replayed from its saved `source.diff` (cumulative
  against d09e5561) and dated at its build. The tip's tree is byte-identical to the stream tree as H44 was built. H33-H43
  carry the previous build's generated SPIR-V data (their saved diffs left it out): regenerate before building them.
- `wishlist/drafts` e33680e9, 485b839e: the early bike draft, the UI motion core (ui_fx), nier_walk.py and its test.
- `wishlist/player-screen` df86ea93 was committed by its session (installed as Install 67 the same day).

Tips for phase 1:

| Branch | Tip | Into baseline/1 |
| --- | --- | --- |
| `renderer/pbr-materials` | 42b077ce | the base |
| `wip/stream` | e5a05b4f | yes (the kits' renderer) |
| `collision/r3-residency` | 41bf7629 | yes |
| `claude/r3-gameplay-fixes-212fe2` | ea6b7c62 | yes |
| `wishlist/xenui` | 2f16d0c7 | yes |
| `wishlist/player-screen` | df86ea93 | yes |
| `claude/vigilant-sanderson-aa0512` (C) | 49a9eb0f | yes |
| `gameplay/tick-independence` (B) | b34547a6 | yes |

Experiment branches against H44 (each branch's own added lines found in `wip/stream`): `wip/water` 94%, `wip/r8`
99%, `wip/v5` 92%, `wip/v6` 80%, `wip/v7` 96%, `wip/v8` 95%, `wip/standin` 92%, `collision/world128` 94%,
`wip/viewcull` merged. The rest of each is lines later builds rewrote; none is merged again. Out: `wip/p3-viewcull1`
(58%, not adopted), `collision/m2` (stopped, evidence), `wishlist/drafts` (drafts), `renderer/hair-palette`'s 12 dirty
files (an older cut of the dress solver, superseded by ffc10a5d). Detached experiment trees (wt-charlight,
wt-install45, wt-col12, wt-col13, wt-preload) hold only edits already committed on the main line (the gore scratch,
the spawn preload); wt-p3-order is the view cull experiment.

**`wip/p3-fixes` settled** (the phase 3 session, 2026-11-06): r_sceneSsrFlat, the r32f reflection depth and r_sceneSsrDenoise were dropped on purpose (the dither's cause was sampling noise, fixed by 7fb72585, in the baseline; docs/levelport-render-district.md). Lost before anyone judged them, and being ported onto a branch from baseline/1 by that session: r_sceneFogTemporal (the haze grid's checker on lamp-lit ground, owner playtest 2026-11-01; default off until gated), r_sceneSpecAA (default off, owner A/B), the window rooms by 3D room cell turned to the facade (the corner-window fix), the window-room debug views and r_sceneLightHideDistant. Merged into baseline/1 before phase 4.

## Phase 1 result (2026-11-06)

`baseline/1` (worktree `.codex-research/wt-baseline`) is the main line at 2e7da623 with seven merges, in order:
`wip/stream` 6de8ca70, `collision/r3-residency` 0e35d8cd, `claude/r3-gameplay-fixes-212fe2` 8c21da46,
`wishlist/xenui` 8bdf9f7e, `wishlist/player-screen` c89e9d61, C 1f61d673, B ebf36a6b. Two conflicts, each two
additions at the same spot, both kept: `tr_init.cpp` (xenui's `r_xenUIRenderApi` and the stage's `r_stageRigWorld`,
`r_stageSampleShading`) and `g_active.c` `G_RunClient` (the chunk hold, then the saber lab).

- **Shaders:** the scene SPIR-V regenerated byte-identical to the merged file; `renderer_spirv.py verify`: 624
  shaders, 0 differ; the cloth shaders are unchanged by every merge (their tool needs Godot, not run).
- **Builds**, fresh, MP targets only (SP and JK2 off), Release, Visual Studio 2022, from this tree alone
  (`.codex-research/baseline-1/build.sh`, logs beside it): x86 (`build_Win32`) 0 errors, 7 warnings; x64
  (`build_x64`) 0 errors, 9 warnings. Every warning is in a file no merge changed (stock NPC_spawn.c, bot2_defender.c,
  stb_dxt.h, winbase.h).
- **Python suite** from the repo root: 491 tests, 1 error, `test_repository_kejim_and_dantooine_reparse`, which reads
  the installed game's pk3s through a repo-relative `game_directory` that only the main checkout has; it passes there,
  and neither it nor its module changed in the merge.
- **tools/tick_rules_sim.py:** runs clean (exit 0), the same tables as B's record.
- **Bike:** `tools/bike/tape_harness.py` with a C build of bg_bike.c and the real pmove test from this tree: PASS, 0
  failed gates (65 pmove comparisons; parked bikes asleep at 4 traces a think).
- **B's 14 bot worlds** (`tick_smoke_b1.py`: dedicated x86 servers, stock bots, 90 s, sv_fps 20-250, g_tick* on and
  off): all started, stayed up, answered rcon, exited 0, no error lines; kills at every rate.
- **Kit loads:** `build/cp_watson_dev-night-29-b1` (night-29 with this tree's x64 exe, renderer and game modules;
  kit29b1.py): 20 of 20 loads reached ref05's solved camera, no crash dump, launch to loaded 24-34 s, peak private
  about 10 GB (runs/b1_loads.tsv under look/cp77ref-20261104). Its ref05 frame against night-29's own: the difference
  is the steam's motion (sheets/solve/b1_vs_n29_diff.jpg), as large as two baseline loads differ from each other.
- **Not run:** an x86 client smoke (menus, player screen, picker): the client helper runs only the installed exe,
  so it comes with Install 68. One Bot2 laps world: left for phase 4 with the timing runs.

## Phase 2 result (2026-11-06, collision still being ported)

**Parity, five tiles.** `tools/levelport/scene_parity.py` (committed) compared night-29 as served (scene 10j and
its eight overlays) with this branch's converter run alone, no overlays: Misty's tile -7_4 (r3fo: tile records and
foliage of tag 10h, interiors step) and the ring -5_8, hill -3_3, interior-heaviest -6_0 and shore -6_11 (b1par).
Every night-29 instance has its match (581,941). Each remaining difference has a cause:
- **Overlays that missed records, now right natively:** 328 + 1,455 lights without the tube shape and reach the
  shapes overlay gave the others (it matched by position and missed lights in scaled placements); 52 + 227 nit
  lights at pi times their power (the same miss in the nit overlay); 102 SpeedTree leaf materials without the
  FoliageProfile alpha cut (10j reused leaf images baked before that fix); 15 font decals (`font_a.mi`, a material
  instance of the vector-billboard template) the vector-board overlay did not flag as screens; spot directions of
  length 2 from scaled placements (round 3's normalisation).
- **Round 3 rules:** 5 + 128 lights with an explicit zero intensity are off. Open: 33 alarm lights and 2 billboard
  front lights (1,600 and 1,500-3,000 lm) are gone natively, consistent with the 'never' time class or a
  non-default sector variant; each to be confirmed.
- **Intended:** every multilayer material's images (the 8-slot bake: 11,477 materials); 396 instances, 1,140
  materials and 17 screens of meshes fetched after 10h (door devices and leaves, pipes, panels, vending and arcade
  screens).
- **Open, likely wrong natively:** 3 `wood_bare_01_ebony.mi` materials get the screen flag (0x200) from the
  vector-board rule; wood should not take the screen gain. To check which template and EmissiveColor they carry.
- **To check in phase 3:** door leaves. The native build draws sliding and roll-up door leaves where night-29 had
  only their proxies; the owner's door rule needs opened doors' leaves hidden, which cp77_tiles passes to the scene
  parts and these one-tile builds did not go through. The tag-11 parts stage shows whether it holds.
- My test setup, not the converter: the first one-tile build lacked the tag's foliage (3,438 litter instances) and
  the interiors step (9,260 slots); both match once present.

**Converter** (c64ab440): the tiled merge writes `.xwat` v2/v3 itself (the ocean record and sea mask; checked
byte-identical to night-29's but for the build's own indices), retiring ocean_xwat.py and its hard-coded mesh;
`--detail-slots` defaults to 8.

**Fetches** (levelport/fetch-closure, merged 2c200122): `tools/levelport/wk_fetch.py` and `fetch` / `needs` modes in
cp77decals.py and cp77fx.py replace the uncommitted census and fetch scripts; the needs walk reaches every path the
old rounds asked for. The effects' rule-based scope covers 32 more particle systems than the old hand cut (more
steam and smoke placements in the tag-11 build).

**Driver** (78e7d9e1): `tools/levelport/build_watson.py <tag> --data <inputs.json>`; stages extract (opt-in),
tiles, indexes, parts, scene, interiors, collision, decals, fx, env, kit, checks; resumable, nothing in place, 25 GB
free on both drives per stage. Dry-run checked against the tag-11 inputs (`.codex-research/baseline-1/inputs-11.json`,
local). The extraction stays CP2077 patch 2.31's (owner 2026-11-05): the patched game can no longer re-make it, so
the clean import starts at tiles. Env, dawn, day and moon are local input files (envpack-h32, the moon texture),
packed by the driver.

**Collision:** the chunk chain stays the shipping collision (the district map was the stopped collision/m2
experiment). The collision session is porting tag10a-c, the hub (hub_inject, spawn data) and the packer into a
committed `tools/levelport/cp77_collision.py` on its own branch; a clean build compiles every tile (the 26 chunks
inherited from tags 8-9 go). The full collision build is about 6-8 hours and 21 GiB on D:.

## Phase 3 progress (2026-11-06, collision pending)

`tools/levelport/build_watson.py 11` from the 2.31 extraction, every stage from committed tools (inputs
`.codex-research/baseline-1/inputs-11.json`, local): tiles 5 min, indexes 8, parts 80 (collision parts, then scene
parts on their doors), scene 5, interiors 1, decals 4, fx 4. Builds: cp_watson_scene-11 / -11-int (268 tiles,
6,696,531 instances, 56,444 materials, 121,615 lights; .xwat v3 natively), cp_watson_decals-5 (347,669 decals, from
313,656: the fetch closure found 19 materials and textures missing), cp_watson_fx-11 (18,594 placements, 36 more
systems), cp_watson_env-11. Tag 11 stays on C: (the owner's NVMe move of 2026-10-31); the superseded kits, scene
builds 9-10h, tag 10h and the day's test builds went to C:'s trash, which the owner emptied.

- **A mistake, fixed:** the first parts run was scene-only to save C:, so no collision part decided the doors and the
  scene drew 3,055 door leaves, casings and handles in doorways the collision opens (the owner's door rule). The
  parts stage builds the collision parts first again (02958f71); the rebuilt scene leaves 2,076 leaves out.
- **City parity** (all 268 tiles against night-29 as served, `.codex-research/baseline-1/parity/city_n29_vs_11b.json`):
  every night-29 instance matched (6,666,534); 979 more, 710 of them sliding-door casings (the frames stay where a
  door opens) and the rest newly fetched lamps, panels and screens. Lights: 27,278 with the shape and 6,672 nit
  lights with the power the overlays missed; 1,022 ad lights recoloured with their ads (one light each side, same
  place); 924 with a zero intensity off (round 3); 15 new entity lights. Materials: 25,541 multilayer bakes (8 slots);
  38 font decals flagged as screens (the vector-board rule).
- **Look** at the solved cameras (kit night-29-t11, renders runs/t11_look; sheets/solve/t11_look.jpg, t11_fg.jpg):
  Misty's foreground has the asphalt's grain and no waves; Misty's window grid reads pink as CP2077's; the ring as
  night-29.
- **Renderer:** renderer/p3-carry merged (f14fd533): r_sceneFogTemporal, r_sceneSpecAA, r_sceneWindowCell (the
  corner-window fix), r_sceneDebug 11/12, r_sceneLightHideDistant, each default 0 (the phase 3 session's gate:
  night-29-b1 at ref05 within run noise of baseline/1; sheets/c1/p3carry_ref05.jpg shows each on). Shaders
  regenerate and verify unchanged; x86 and x64 rebuilt, 0 errors. docs/xenjk-cvars.md predates H14 (to regenerate).
- **Open:** 33 alarm lights gone (to confirm the rule); 4 ebony wood materials and 1 recolour material changing the
  screen flag, 6 interactive-light and UI materials at the 0.1 emissive floor (to check against CP2077).

## Phase 3 result (2026-11-07)

**The kit:** `build/cp_watson_baseline-11` (build_watson.py's kit stage): baseline/1's x64 engine, renderer and
game modules (rebuilt after the p3-carry merge), tag 11's scene (cp_watson_scene-11-int), collision and hub
(cp_watson_collision-11), decals (cp_watson_decals-5), fx (cp_watson_fx-11), env and moon (cp_watson_env-11), the
settings and launchers; no overlay pack, nothing from an older kit.

- **Collision** (cp77_collision.py, merged aa088964; 20:39-00:15 with the hub in parallel): 195 chunk BSPs (night-29
  has 195; 54 tiles wholly outside the void kill skipped), 24 GB of BSP, the hub (42,265 light-grid samples, as the
  collision session's check build). Compiled with q3map2 xenjk7 (chunks) and xenjk8 (hub). One tile (-4_-3) was
  never started by the first run and the tool exited non-zero; the rerun compiled it in 141 s (reported to the
  collision session). From 22:30 the build ran BelowNormal on 16 of 20 cores (the owner on a voice call).
- **Checks:** the driver's parity (five tiles) as the city-wide run: every night-29 instance matched. The kit loaded
  at ref05 and ref09 (runs/base11_look under look/cp77ref-20261104), no crash; against the render-side look kit
  (first, door-bugged scene) the frames differ by steam, the ring's ads, window rooms and the door leaf beside
  Misty's door, which the door rule now leaves out (sheets/solve/base11_vs_t11.jpg).
- **Driver fixes found by the run:** the kit stage takes --bin explicitly and sets an earlier .partial aside.
- **Disk:** C: had 80.9 GB free at the start of collision (tags 10 and 10h and the superseded builds are gone).

Phase 4 next: levelcheck on the kit, the 55-camera spread against night-29, timing alone, 20 loads, the owner's
playtest; then Install 68 on the owner's word.

## Phase 4 (2026-11-07, in progress)

- **The 33 'alarm lights' are not a loss:** night-29 had two lights at each spot, the alarm nest's template light at
  the default power 1 and its instance light at 1,600 lm; tag 11 keeps the instance light alone, as round 3's rule
  says (an instance light replaces its template's). Checked in tile -6_0's tables. The earlier classification had
  matched the dropped power-1 duplicates to the 1,600 lm source at the same place.
- **The ebony wood is not new:** night-29 drew the same four wood_bare_01_ebony.mi pieces (Japanese apartment walls'
  destruction meshes) as emissive vector boards with the same image; tag 11 adds only the screen flag the vector-
  board overlay missed. Whether CP2077 lights them at all is a pre-existing question for a look round.
- **Spread** (55 cameras, the kit against night-29 on the same binaries; runs/p4_spread_base11 and _n29b1 under
  look/cp77ref-20261104, sheets/solve/p4_spread_top.jpg): median view 0.9% of pixels changed, mean luminance
  -0.5 of 255. The largest: two ad screens showing other ads from their pools (sp49, a rooftop board, -17
  luminance; sp11, the median strip; the ad index was rebuilt for tag 11), lit-window patterns that vary per
  load (sp17, 40, 51, 52, 15), reflections and steam. No regression found.
- **Loads:** 20 of 20 at ref05, no crash dump (runs/p4_loads.tsv), launch to loaded 27-91 s (the slow ones
  beside the collision session's checks), peak private about 10 GB.
- **Timing alone** (owner: "It's free now"; the collision session's checks paused): the 55-camera spread held 300
  frames each, baseline-11, night-29-b1, baseline-11 again (runs/p4t_* under look/cp77ref-20261104; no other
  client in any run). GPU frame median 5.76 ms (two baseline runs) against 5.73; run-to-run 0.23 ms median,
  0.79 at worst; baseline minus night-29 per view -1.11 to +0.83, median +0.02. Ten views slower beyond noise
  by 0.3-0.83 ms (sp00, 05, 29, 36, 38-42, 44): of their 5.9 ms, 2.1 in the scene pass (8 detail slots, the
  door casings and props now drawn), 0.4 in the blend pass, the rest in unnamed passes (likely the 34,000 more
  decals). The slowest view 7.9 ms.
- **Movement stutter, found and fixed** (owner: stutter in motion since Watson grew; runs/stutter_a-d, _fix1b,
  _fix2, _fix2g under look/cp77ref-20261104, at the owner's settings: 4K, MSAA 2, 250 cap). Standing still no hitches;
  moving 1.3-3 frames a second of 20-60 ms, all in the renderer's CPU front end (GPU, server, game and collision
  chunk swaps flat); r_drawworld 0 or r_lockpvs 1 removed them. Cause: the hub BSP (-vis none) has 1,116,514 leaves
  in about a million clusters and 12 surfaces; R_MarkLeaves walked every node at each cluster crossing and the
  tree walk cost about 5 ms a frame. Fix 53551ce8 (a no-vis world marked once; a few-surface one not walked):
  hitches 2.0-2.6/s -> 0.0-0.7, frame p99 28-31 -> 18-23 ms, front end median 5-6 -> 0.1 ms; frames now
  GPU-bound (about 9 ms at 4K), the rest of the hitches scattered GPU variance. ref05 and ref09 unchanged within
  noise. Kit `build/cp_watson_baseline-11b` (build_watson --kit-name) carries it; baseline-11 stays for the level
  checks in progress. The owner's playtest notes and log: `.codex-research/baseline-1/playtest-20261107/`.
- Level checks (the collision session, `.codex-research/levelcheck-phase4-20261107/`): the void kill identical on
  both kits; baseline-11's 19 spawns pass the offline check. Running: per-tile compare and drop on both kits
  (about 5 hours each), the door check (on its first 34 doors, the opened double doors' openings are blocked by
  frame or casing collision: whether drawn or invisible is being found), the engine spawn gate and residency smoke.

## Owner decisions (2026-11-06)

1. Import: code-keyed caches plus three tiles built cold, byte-for-byte.
2. Look defaults: the current kit values (H44 wet ground 0.5 / 0.4, r_sceneReflect 3, 8 detail slots).
3. Gameplay: B and C go into the baseline; saber 125 Hz stays open on top of it.
4. Install: a kit first; Install 68 (Install 67 went to the player screen the same day) and `master` after the owner has played it.

The options as asked:

1. **Clean import depth.** Code-keyed caches plus a cold control on three tiles (several hours), or everything cold
   from the extraction (much longer; the fetch alone is hours).
2. **Look defaults to settle first**, since the baseline bakes them in: the wet ground H44 (darkening 0.5, SSR fade
   0.4), r_sceneReflect 3 (fitted by eye) or 1 (physical), r_sceneDetailSlots 8 (chosen 2026-11-04).
3. **Gameplay branches.** B (tick independence) still misses its saber target at 125 Hz; C left the command cadence
   question. Take both in now, or the level port and renderer first and B and C after.
4. **Install.** Install the baseline as Install 68 and move `master` to it, or keep it a kit until the owner has
   played it.
