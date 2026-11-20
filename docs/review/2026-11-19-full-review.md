# Full review before publishing (2026-11-19)

Owner, 2026-11-19: "full code review by fable 5.2 medium, we are going to publish and want everything clean", with
"make the code easier to lift where it's tangled" and the guide from OpenJK to xenjk ([docs/guide/](../guide/README.md)).

## What was done

1. **Review** (Fable 5.2, medium, read only) of everything xenjk wrote since the OpenJK fork, in nine lanes plus three
   second passes where a lane had not read every file. Bot2 and character rendering, fully reviewed on 2026-11-09,
   were reviewed for their changes since. Reports, one per lane, in [2026-11-19/](2026-11-19/); each finding has a
   `Status:` line (fixed with its commit, not a bug, or deferred with the reason).
2. **Fixes and untangling** (one branch per lane, merged into master): every finding verified before it was fixed;
   then the mechanical liftability moves the reports proposed.
3. **Publish cleanliness:** no private path, name or scrub pattern in any tracked file; every tool reads outside
   locations from arguments or environment variables (no machine defaults); licence headers on xenjk's files; process
   notes (owner dates, review IDs, research-folder citations, agent names) out of code comments and tool help;
   game-specific content read from content (below).
4. **Checks:** a fresh Win32 and x64 build of every multiplayer target (0 errors; only warnings that predate the
   review); the committed SPIR-V regenerates byte-identical; the unit tests (511) pass; comment-only sweeps checked by
   comparing the comment-stripped code; a native smoke of three launchers on the x64 build (the player screen and
   modules load on Tifa's showcase, Kabuki and the Beauclair castle); Beauclair stills at the integration poses match
   the pre-review build except animated water.

| Lane | Findings | Fixed | Not a bug | Deferred |
| --- | --- | --- | --- | --- |
| [scene](2026-11-19/scene.md) (render scene) | 57 | 45 | 2 | 10 |
| [resources](2026-11-19/resources.md) (loading, images, VRAM) | 12 | 11 | 0 | 2 |
| [shading](2026-11-19/shading.md) (frame, shaders) | 18 | 14 | 1 | 3 |
| [characters](2026-11-19/characters.md) (hair, cloth, Ghoul2) | 16 | 14 | 0 | 2 |
| [engine](2026-11-19/engine.md) (qcommon, client, server) | 22 | 21 | 0 | 1 |
| [game](2026-11-19/game.md) | 20 | 13 | 0 | 7 |
| [cgame-ui](2026-11-19/cgame-ui.md) | 29 | 24 | 1 | 4 |
| [tools](2026-11-19/tools.md), [tools-2](2026-11-19/tools-2.md) | 40 | 34 | 0 | 6 |
| [levelport](2026-11-19/levelport.md), [-cp](2026-11-19/levelport-cp.md), [-w3col](2026-11-19/levelport-w3col.md) | 72 | 70 | 0 | 7 |

No finding was P0. A finding fixed in part is counted as fixed; its Status line names the deferred part.

## Owner decisions taken (2026-11-19)

- Process notes out of code comments; the dated docs keep the history.
- `s_volume` behaves as stock (0 mutes effects only); `s_mute 1` silences the whole mix (capture launches set it).
- The body-jiggle springs (`hair_spring.c`) rewritten from the plain damped-spring model, not from another engine's
  source; they match the earlier implementation to rounding on all 174 spring assets offline.
- Code that only makes sense with our converted content reads it from the content: the CT-3X handling from the bike
  pack's `ext_data/vehicles/<name>.bike` ([bike-handling.md](../bike-handling.md); `bg_bike_ct3x.h` removed), the
  picker's source tabs from `character.cfg` (`source`, `sourcelabel`, `sourcename`, `sourceorder`), the crossing
  sounds from the map's `.signals` (`sound <role> <path>`).

## Open: owner decisions

- **What ships:** test and lab code in every build or behind build options (the saber lab and benches now sit behind
  `BuildGameLabs`; the cgame aim lab, bench and conc-trace tools; the UI agent hooks `ui_menuitem`, `ui_roster`,
  `ui_xen*`; the chunk test-fault cvars; `bot2_servercost`; renderer debug switches `r_charHairMap16`, `r_fmaDebug`,
  `r_g2SampleReport`, `r_charOcclusionDebug` 5 and -1, `r_portraitRig 2`, `charshadowdump`, `r_arrayDump`; the Bot2
  measurement code `r_g2LodStats`, `r_g2BlendStats`).
- **Remaining review switches:** which `r_fix*` / Bot2 `bot_moveFix*` switches stay as settings and which old paths go.
- **Defaults:** `r_smoke` on by default with its commands; score plums on every hit for stock clients (RG-4); the
  crash-dump handler default (RG-3); `r_scene 0` on level-port maps (RS-9); exposure metering without MSAA (RS-8);
  a map's `.xenv` sun when its scene is missing (RS-26); the parity verdict on missing tiles (RL-203); supported
  `sv_fps` range for the forecast ring (RG-15).
- **Provenance wording:** `skirt_sim.{c,h}` (a port of our ClothResearch JS prototype), `hair_sim.h` (a reading of the
  source games' Bonamik parameter set), `w3emitter.py` (record layout from WolvenKit 7, GPL-3.0), `w3grade.py` and the W3
  Blender scripts (the Witcher3-Blender-Tools add-on's functions at run time), `vk_gputime.cpp` / `vk_sunshadow.cpp`
  (ours or JKSunny's header), `bg_bikemove.c` / `BikeNPC.c` (Raven/Activision lines), `cg_cameraFPS` (EternalJK /
  TaystJK), the RmlUi GL3 filter matrices in `xenui.cpp` (MIT), licence headers on shaders, the q3map2 patches' GPL
  note.
- **What goes in the tree:** the xenui RML/RCSS sources for the player screen (four variants exist only locally), the
  client-check harness (`gw_common`, `cc`, `game_lock`), the generators of `signal_groups.json`, the ads, the sea
  mask and the Watson `.xenv` packs, the vendored `tools/cloth/blender/jediacademy` upstream record, the five cloth
  scratch scripts, sibling project names used as defaults.
- **Localisation** of the player screen (RU-29).

## Open: needs a game run

Each lane report lists its run-dependent items (status `deferred: ... run`). The main ones: the hair solver's look
while turning (RC-1, confirmed offline), `sv_pure` tangent-cache hits (RR-1), the oversize-DDS guard (RR-3), the
xenui texture leak design (RD-3), validation-layer runs for the scene's stream frame (RS-3, RS-38), the remote-server
input queue (RE-1), the bike and the crossings with their new content files.

## Installed game

Install 95 (2026-11-20, renderer-vulkan-in-xenjk.md) runs this master, both clients,
with the content it needs: the bike pack carries `ct3x.bike` (and bike v3's `VH_BIKE` vehicle, model and sounds,
never installed before), the characters pack has the tabs' order and long names, and Kabuki's `.signals` names its
four crossing sounds. Night City's kit keeps its own binaries and has no `.signals` (only Kabuki has traffic signals).
