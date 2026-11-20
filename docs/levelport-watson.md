# Watson levelport: scope (2026-10-30)

Owner, 2026-10-30: "can we scope out getting all of watson in game? i think we can exclude areas that are kinda
empty, arasaka waterfront can be excluded too i think. we will need really solid processes and faster compiles,
and some streaming/loading improvements etc"

This is a scope, not a plan of record: what the Kabuki block costs today, how big Watson is, what breaks at that
size, and the work that would get it in; sections 7-10 record what phase M measured and built since. The Kabuki port it extends:
[levelport-kabuki.md](levelport-kabuki.md) (handoff and results), [levelport-scene.md](levelport-scene.md) (the
render scene design).

## Resume here (handoff, 2026-11-01 ~02:15)

**State at 2026-11-06 ~11:50 (newest; overrides everything below where they disagree). Handoff before compaction.**

- **Kit to use: night-29** (`build/cp_watson_dev-night-29`, `cp77/watson/kit29.py`; not installed; the owner has not
  played 25-29 yet, said "Not yet"). Chain, each a hard-link copy of the one before plus:
  - night-25: renderer H37 + envpack-h32 (night haze at CP2077's fogHeight 125, anisotropy 0.7, light 0.5, soft ceiling
    off; H32 integrates each light along the ray).
  - night-26/27: CP2077's projected decals (H42/H43 + build cp_watson_decals-3/-4; 313,161 drawn; road paint tiled).
  - night-28: H44 (wet porosity retune 0.5 / 0.4, the owner's look choice still open) + mlrough1 overlay (multilayer
    roughness levels read as CP2077 defines them).
  - night-29: nit overlay cp_watson_scene-10j-nit-x0.3183 (nit-unit lights at 1/pi).
- **Current task (owner): Misty's foreground ground.** Owner's crop of night-29's street: "this is not fixed" (blurry,
  wavy, cyan blobs, no grain). Owner rules: "find the real issue before tweaking"; "mistys ground is enough".
  - Found 1 (fixed): nit-unit lights converted pi times too strong (cp2gltf scene_lights now luminance x area, no
    Lambertian pi; uncommitted). Misty's lower-half hue now matches CP2077 (0.51 0.27 0.21 vs 0.51 0.30 0.20).
  - Found 2 (confirmed 12:35): the foreground's blur and waves are the 1-slot multilayer bake. With one detail slot
    every layer but the main is baked into one 512 px image per mesh, so a tiled asphalt normal and colour spread over
    tens of metres alias into long waves and blobs (the lamps' highlights turn them cyan). The owner chose 8 slots on
    2026-11-04 (.xdet v2; renderer r_sceneDetailSlots, default 4) but the city scene 10h/10j was built WITHOUT
    --detail-slots (its .xdet is v1). Test: `cp77/watson/r3ds.sh` built Misty's tile -7_4 alone with --detail-slots 8
    and today's converter (build cp_watson_scene-r3ds); scratch kit build/cp_watson_dev-night-29-r3ds (kit29r3ds.py:
    night-29 without the city scene packs and their 10j overlays; renders at Misty's only, not for play); renders
    look/cp77ref-20261104/runs/n29r3_detail2 (cams_m05detail2.txt; use the .jpg shots, the _png ones fire before
    benchcam sets the pitch). Sheets sheets/solve/fg_r3ds_close.jpg and fg_r3ds_road.jpg: the waves and blobs are gone
    even at r_sceneDetailSlots 1 (the layers are no longer in the bake); at 4 or 8 slots the road gets CP2077's fine
    wet asphalt grain and the slabs are crisp. The cause is the slot count, not the levels fix (that touches roughness
    and metal only). **Next (awaits the owner's go-ahead):** the city scene rebuilt with --detail-slots 8 under a new
    tiles tag (one tile re-baked in 11 min at 12 workers; 268 tiles, mostly cached materials; C: had 63 GB free), then
    the 10j-keyed overlays redone on it (ads, foliage, holo, xscr, rotation, interiors, ocean; shapes, nit and
    roughness come natively).
  - Also found: screen reflections at r_sceneReflect 3 (fitted by eye; 1 physical) mirror facades on ref07's road;
    procedural puddles change nothing measurable; albedo checks out (asphalt ~0.08 linear); the wet model is a minor
    part (dry, ref05's lower half is still too bright). ref05's upper half runs bluer than CP2077 after the nit fix
    (another source, open).
  - Method caveat: only ref05 and ref09 have solved cameras; cams_n23's ref06/07/08/12 are different shots from
    CP2077's, so "gap over six views" numbers mixed different pictures. Judge on ref05 (and ref09) until solved.
- **Other open items:** Gomorrah's sign orange vs CP2077 pink (signages.mt single colour); the ring still darker than
  CP2077 (ref09 55 vs 72); effects rebuild cp_watson_fx-3 waits for the renderer's alpha clamp and format flags (asked);
  decals v2 emission untested on screen; Q307 lighting state tested and rejected (build q307-2 kept, not in kits).
- **Uncommitted (repo):** tools/levelport/cp2gltf.py (nit pi fix), cp77mat.py (levels(), levels_slope(), bake tag
  |lv2), cp77decals.py (new: CP2077 decals -> .xdcl v1/v2; contract look/decals-20261106/CONTRACT.md), README.md, this
  doc. Last commit ad5d22b1 (pipeline + round 3 fixes). Out of repo (cp77/watson/): patch_fogonly24, patch_wlc25,
  patch_q307_25, patch_nit25, patch_spotcone28 (rejected), patch_mlrough27, kit25-29, r3ds.sh, kit29r3ds.py.
- **Sessions:** renderer (local_1b203a4d, idle; builds H31-H44 in look/stream-20261104/dll, notes in
  cp77ref-20261104/README.md; queued ask: .xfx alpha clamp / frame flags / lifetime flipbooks; scene v7 diffuse/specular
  gains). Collision (local_d10492f0, idle): round 3 fixes on branch collision/r3-residency (054de50f, fb0fa1a1), two
  smokes waiting for free RAM. Gameplay r3-gameplay (local_6f29f495, idle): branch claude/r3-gameplay-fixes-212fe2,
  six commits (bike pmove, picker, pk3 fast reads, rider lean ea6b7c62 awaiting the owner's look). Nothing merged or
  pushed. Round 3 review: look/review-20261106/REPORT.md.
- **Trash this session:** .codex-trash/20261106-0425-fogonly-wrong-type, -0545-q307-1-manifest, -fx3-catmullrom,
  -0910-decals-superseded, -light-class-tests (each with MANIFEST.tsv).

**State at 2026-11-05 ~16:15 (superseded where the block above disagrees).**

- **Owner, this round:** after the reference compare, "address sky ... moon add is key", then items 3 (ring screens in
  their halo) and 4 (overbright alleys), and "not only signs and trees" (floors, smoke emitters). Moonlight direction:
  the owner left it to us ("as long as the moon exists in the sky").
  - **Night-20** (`build/cp_watson_dev-night-20`, kit20.py): night-19 + renderer H24 (c2db014f, CP2077's moon on its
    path `r_sceneMoonPath "35 -2.5 23.85"`, disc and glow fitted to ref09 within 2 px) + `zz_cp_watson_zz_moon.pk3`
    (build `cp_watson_moon-1`, textures/skies/moon.png). Moonlight stays the map sun (r_sceneMoonLight 0: centre
    gaps 159 against 172 for the disc's).
  - **Compare round** (sheets/n19): night-19's fixes hold where those classes appear; street views unchanged.
  - **Item 4 cause:** not light channels (patch_lightch20.py zeroed 8,891 unconfined non-world lights: under 1 %).
    The point model of CP2077's tube and panel lights: zeroing the LU_Nit areas near ref05 / ref06 takes the centre
    107.5 -> 80.8 and 127.8 -> 94.1, static lumen lights 89.2 / 96.2 (CP2077 73.7 / 76.9); open streets are not
    over-lit. Fix: scene version 6 (a 10th section, each light's half-tube along CP's local X and source radius;
    header 88 B; manifest tile lines' 12th number), written by cp2gltf.light_shape and, for 10j, by
    `cp77/watson/patch_shapes20.py` (build `cp_watson_scene-10j-shapes2`, 159 tiles, 50,965 tubes; light size ignores
    the entity's scale). The renderer session builds H26 to draw them; renderers before it cannot read version 6.
  - **Item 3:** the ring's halo is seven haze-only designer lights; the class is the screens' brightness (H25, renderer
    session, sweeping).
  - **ref09's far ad tower:** nothing draws at its bearing even haze-free (the City Center skyline ~2 km out), though
    its billboard proxies are in placements.json. Open.
  - **Unconverted classes** (`cp77/watson/census_fx20.py`, census_fx20.txt): 357,470 worldStaticDecalNode (dirt, dust,
    gravel, grime patches), 30,933 worldStaticParticleNode + 3,912 worldEffectNode (steam, smoke, dust motes,
    sparks), 520 cables, 617 far terrain proxies. Owner to pick decals or steam first.
  - **Timing** (alone, 600 frames a spot; `runs/n19_ms.txt`, `runs/n21_ms.txt`): at ref15 (night) night-18 6.41 ms,
    night-19 6.66, night-21 6.78; at ref04 (dawn) night-19 6.71, night-21 6.81. Today's toggles at dawn: clouds
    0.04 ms, holograms and canopy shading within noise. The whole day's work: +0.37 ms at the hill (~147 fps).
  - **Night-21** (`build/cp_watson_dev-night-21`, kit21.py, 17:02): night-20 + renderer H27 (2f8aa654: H25 screens
    keep their colour when bright, r_sceneScreenGain 3 / bloom 0.25; H26 version-6 reader, r_sceneLightTubes 0; H27 eye
    adaptation, a metered exposure within CP2077's window for the hour) + env pack `cp_watson_env-em32-dawn4`
    (r_sceneExposureWindow: dawn -1.2 4.44, day -1.2 5.38; night the default -1.2 1). Centre gaps to CP2077 (sheets/n21):
    ref05 +37.8 -> +13.0, ref06 +51.1 -> +20.1, ref07 -20.7 -> -16.7, ref12 -17.0 -> -8.3, ref04 dawn -2.1.
    **Item 4 correction:** tube lights (H26) made the alleys brighter; the cause was the fixed exposure. The shapes pack
    stays out of the kits (tubes off it draws as without it).
  - **Steam and smoke** (owner's pick over decals): `tools/levelport/cp77fx.py` reads CP2077's .particle / .effect
    (WolvenKit-serialised, `look/fx-20261105`, fetch_fx.sh) and the sectors' worldStaticParticleNode /
    worldEffectNode into maps/cp_watson.xfx (layout in its docstring) + flipbook DDS: build `cp_watson_fx-2`
    (18,084 steam, smoke, dust and water emitters; flies, sparks and fizzles not fetched yet, 15.4k nodes). Renderer
    H28 (3438388a) draws them as stateless GPU particles and drops the .xfog's steam columns. **Night-22**
    (`build/cp_watson_dev-night-22`, kit22.py) = night-21 + H28 + the fx pack. Timed alone (`runs/n22_ms.txt`): the
    particles' blend pass 0.06-0.12 ms; frame totals within the run's noise (the desktop holds the GPU at 23-32 %:
    timing scripts' idle bar is 50 %). Open: flipbook blending, wind, stretched / oriented quads, one sparks emitter
    with an empty texture path.
  - **Misty's (ref05), owner: "take Misty's scene and work it"** (2026-11-05 ~22:30). Solved camera `cams_ref05s.txt`
    (least squares on hand-picked pairs `pairs_ref05b.txt`, `classes/solve_manual.py`; SIFT fails across the look gap;
    position maps need particles, glow and haze off: `cams_ref05_pos.txt`); regions `classes/regions05.py`.
    **Night-23** (`build/cp_watson_dev-night-23`, kit23.py) = night-22 + renderer H29 (fba968e2: steam fainter) + env
    `cp_watson_env-em32-dawn5` (new maps/cp_watson.xenv: night haze at CP2077's VolumetricFogAreaSettings, density 0.003,
    light 0.9; the lamps' in-scatter was a grey veil) + `cp_watson_scene-10j-vb1` (patch_vboard22.py: 64 vector boards
    as screens; cp77mat / cp2gltf `vector_board` -> 0x200). Matched views' summed gap 198 -> 177; ref05 regions: overhang
    +58 -> +23, upper facade +49 -> +29, Red Room -60 -> -29. Costs: the ring views lose some halo (ref09 centre 88 -> 61,
    CP 88): the ring screens' own brightness and bloom are the next class there. Open at ref05: signages.mt tinted by
    ColorOneStart alone (2,807 materials one colour; CP picks ColorOne..Six per glyph), the left alley's even lamp light,
    per-surface wetness (the ground), the Esoterica glyph panel behind shop_al_glass_mistys_darker.
  - **The ring, owner: "cp2077 looks like a screen emitting light and ours looks like a light next to a screen"** (23:05).
    Its halo is seven fog-only area lights ({L_Zapiekanki_Main_Fog_}01-07: 20,000 lm, 10 m capsules, source radius 2 m,
    220 degrees outward), one band round the ~70 m rim in CP2077, seven blobs as our points; its screens are
    billboards_material at EmissiveEV 3-3.5. Renderer H30 (0be9826a): r_sceneLightTubes 2 (default: only the haze-only
    lights take their shapes, 4,540 of 121,452; surfaces stay points) and r_sceneScreenGain 4.5. **Night-24**
    (`build/cp_watson_dev-night-24`, kit24.py) = night-23 + H30 + `cp_watson_scene-10j-shapes2`. Matched views 177 (as
    night-23); ref09 centre 61 -> 63, ref12 35 -> 36 (CP 88 / 59): the ring glows from its screens now, still dimmer than
    CP2077's. Cost: ref05 clips 1.1 % -> 6 % (the billboard and Red Room); gain 3-4 is the fallback.
  - **Ring, "Strengthen" (2026-11-06).** Owner: CP2077 makes both the ring and Misty's from the inputs we have, so a
    lever that trades one for the other is the wrong lever. `cp77/watson/patch_fogonly24.py` scales the haze-only lights'
    power; its first packs (fogonly-x2/x4/x8, in `.codex-trash/20261106-0425-fogonly-wrong-type`) read the type at byte
    44 (the cone; the light record is origin, radius, colour, power @28, direction, cos outer @44, cos inner, type @52,
    box, fog) and so scaled 13,408 full-sphere lamps: Misty's blew out, the ring did not move. Fixed: `fogonly2-x*`
    (7,919 haze-only lights). At fixed exposure x8 lifts ref09 by 30 levels (it did nothing before). As played (cams_n23,
    `score_fo2.py`, sheets/solve/fo2_*): ref09 56 -> 66 (x2) / 78 (x4), CP 72; ref12 46 -> 55 / 65, CP 64; ref05 86 ->
    91 / 100, CP 74; summed gap x1 75, x2 70, x4 82. CP2077's own haze height (fogHeight 125 m; ours 80) lifts the ring
    (ref09 +4.4, ref12 +1.1) and leaves Misty's (+0.2): gap 75 -> 70, with x2 65. Not in a kit yet. Still open: at x1
    the ring is dark and Misty's bright, so another input differs by class (Misty's haze-only lights are mostly
    lumen spots, some with EV 1.7-2 on top; the ring's are 220 degree lumen capsules); no environment area covers either.
    Traced further (owner: "keep tracing"): CP2077's haze anisotropy (globalLightAnisotropy 0.7 at 01:00; ours 0.5)
    helps a little (gap 70 -> 65; runs n24_phase_x*). At Misty's, fixed exposure, the haze's light makes the lift
    (darkest tenth 38 with it, 14 without; CP 18), mostly from the haze-only lights (20 without them). Misty's far-left
    blue building (owner: "very lit in ours and totally shadowed in cp2077") is lit by {L_RedStreet_Top_Main} (50,000
    nit, 20 m capsule, no shadows in CP2077 either). It and the two {L_RedStreet_Top_Fog} haze spots are in the sectors'
    WLC_03_Default variant (on by default); each sector also carries WLC_03_Q307 (off) without them and with its own
    set. `cp77/watson/patch_wlc25.py` (build cp_watson_scene-10j-wlc1: WLC_03_Default's 106 lights zeroed, Q307's not
    added) at ref05: centre 86.5 -> 72.7 (CP 73.6), darkest tenth 31.6 -> 13.5 (18.1), left third 86 -> 31 (48), the
    building dark, the veil gone; ref06 loses its floodlit facade but drops to 57 (CP 76): Q307's lights are needed.
    Next: a converter option to pick the variant state (WLC_*_Q307), then rebuild the affected tiles.
    **Q307 tested, owner: "Yes, match my shots" (06:00):** `cp77/watson/patch_q307_25.py` (build
    cp_watson_scene-10j-q307-2: the 16 sectors scanned in both states; Misty's tile 118 default lights out, 54 Q307 in
    their places, 13 default objects hidden, 5 Q307 objects in their records; 30 lights appended in four tiles and
    the manifest's tile lines rewritten; q307-1 lacked those lines and the renderer refused the tiles, in trash). It
    does not match the shots: Misty's turns cold cyan (CP warm red), Gomorrah's interior goes dark (lit in CP); sheets
    sheets/solve/q307_ref05/06. The default set is the street's normal lighting (Gomorrah's interior, Red Room, the
    signs); the shots lack only its pale-blue overhead set ({L_RedStreet_Top_Main}, 50,000 nit 20 m capsule, and the two
    60,000 lm {L_RedStreet_Top_Fog} haze spots, z 45), which no quest reference, light-time class or channel switches
    off. Open: whether CP2077 draws that set much dimmer (a units or haze-model class: Fable round 3 A-D1, the haze's
    froxel-centre sampling counts near lights fully and far ones at about a quarter) or it is off in the owner's game.
    Kits keep the default state. Round 3: Fable's four reports are in look/review-20261106; Astra's run one by one.
    **Owner checked in game (1:07-1:20 AM, several LUTs):** the overhead light is on, as a small spot with a faint
    beam; nothing under it takes the pale-blue wash ours gives. So the state is right and the class is too strong.
    LU_Nit class scaled (`cp77/watson/patch_nit25.py`, builds cp_watson_scene-10j-nit-x0.5 / x0.25, 33,289 lights):
    darkest-tenth gap over the matched views 40 -> 35 -> 31, centre gap 66 -> 63 -> 67; Misty's darks stay 26-28
    (CP 18). The lift is the haze: the class fix is the renderer's haze integration (Fable A-D1) and then one haze
    constant calibrated over all matched views; sent to the renderer session as H32 after H31 (wet ground), with
    CP2077's fogHeight 125 and anisotropy 0.7 in the night xenv.
  - **Round 3 reviews done:** `look/review-20261106/REPORT.md` (both reviewers, all four areas: fix-first table and
    performance table with owners). Nothing fixed yet.
    Owner: "Yes, work through them" (~07:00). Split by owner: renderer items to the renderer session (after H31/H32),
    collision items to the collision session, the bike, picker and pk3 reads to a new session (r3-gameplay, its own
    worktree). Pipeline items fixed here, uncommitted: `cp2gltf.shown_records` (every sector reader takes the
    default variant ranges: sector_fogs, cp77fx, cp77_foliage, cp77_rotate, cp77_ads), scene_repack / vcache read
    version 6, a failed .partial is set aside, patch_chunks hides door leaves in place (stored pack sorting after
    every overlay). Open: no tag-10h door leaf matches a scene instance (about 1.1 m off), so the door patch hides
    nothing there; the reviews' P3 pipeline items (cp77fx curve units, flipbook stems, ads PNG cache, ep1q interiors).
    Owner: "Yes, do those too": the lower pipeline items fixed (coordinator in cp2gltf, two agents in cp77fx and the
    tools/caches), all uncommitted; list in look/review-20261106/REPORT.md. One tile + tiled merge runs end to end
    (build cp_watson_scene-r3check); effects rebuilt as cp_watson_fx-3 (not in a kit). The next run of an existing
    tag rebuilds every scene part. Renderer asks sent: scene v7 diffuse/specular gains, .xfx frame and flipbook flags.
    Committed as ad5d22b1 (owner: "Yes, commit them").
  - **Night-25** (`build/cp_watson_dev-night-25`, `cp77/watson/kit25.py`, 08:16) = night-24 + renderer H37 (2753f3d5:
    H31 wet ground by CP2077's porosity, H32 the haze integrated along the ray, H33-H37 round 3's renderer items) +
    env pack envpack-h32 (af6eca2e: night haze fogHeight 125, anisotropy 0.7, light 0.5 over the six matched views,
    soft ceiling off). Smoke at cams_n23 (runs/n25_smoke): loads clean; centre + darkest-tenth gap 120 -> 106. Still
    open: the ring views dark (ref09 55.5 / CP 72, ref12 42 / 64); Misty's centre bright even haze-free (78 / 73.6);
    the wet foreground still a wavy gloss where CP2077 shows grime and grain (likely the unconverted decals,
    357k worldStaticDecalNode); Gomorrah's sign orange where CP2077's is pink (signages.mt single colour). The
    effects rebuild cp_watson_fx-3 waits for the renderer to clamp effect alpha (up to 10 now).
  - **Decals** (owner: "Convert the decals"). `tools/levelport/cp77decals.py` (new, uncommitted) converts Watson's
    worldStaticDecalNode boxes (347,669 visible; 1,073 materials fetched with their parents, templates and 1,589
    textures, 478 of them embedded in the .mi files: `look/decals-20261106`, fetch_batched.sh, embed_tex.py) to
    maps/cp_watson.xdcl (contract `look/decals-20261106/CONTRACT.md`). Build cp_watson_decals-3 (v1: 313,656
    decals in night_city_b, 6,998 material records, 850 images, 341 MB); cp_watson_decals-4 (v2: tiled
    terrain-projected patches and road markings, emissive decals; 917 images). Renderer H38-H42 (0406f468) projects
    them into the DBuffer (a roughness/wetness target; back faces only; r_scenePDecalTerrain 0 leaves flag-4 patches
    out until the v2 shader). Cost alone at 2000x1125: GPU +0.14 to +0.46 ms, CPU 0.11 ms. Look: ref05's wall tag
    and street graffiti match CP2077; alleys get graffiti, cracks, grime. Open: dark wet road patches (the wet model's
    porosity darkening on rough patches; renderer to fix the base asphalt's roughness first); the wavy gloss is the
    lights' specular on the asphalt normal map at roughness ~0.29, not the ripples.
  - **Night-26** (`build/cp_watson_dev-night-26`, kit26.py, 09:34) = night-25 + renderer H42 + zz_cp_watson_decals.pk3
    (decals-3). Smoke (runs/n26_smoke): loads clean, 293,610 decals drawn. Not installed.
  - **Night-27** (`build/cp_watson_dev-night-27`, kit27.py, 09:45) = night-26 + renderer H43 (f380f9be: decal
    format v2, the terrain-projected road patches and markings with their tiled colour, normal and roughness;
    emissive decals into a fifth DBuffer target) + decals-4. Smoke (runs/n27_smoke): loads clean, 313,161 of 313,656
    drawn (75 untextured patches out). Road paint reads (sp29 "SLOW", sp08 arrows, ref12 lanes); cost as H41-H42
    (+0.12 to +0.40 ms at 2000x1125, CPU ~0.12 ms). Emission untested on screen (no emissive decal in the ref views).
    Parallax height not drawn. The kit to try; not installed.
  - **Street roughness** (renderer session's finding): cp77mat's multilayer bake read CP2077's roughness levels
    backwards and ignored the input range (pairs are [value at white, value at black]; the template's own 'null' entry
    is not always the identity). Fixed in cp77mat (levels(), levels_slope(); bake tag |lv2), uncommitted. Overlay
    `cp77/watson/patch_mlrough27.py` -> build cp_watson_scene-10j-mlrough1 (zz_cp_watson_scene_zz_zzzmlrough.pk3,
    679 MB: the served materials.xscn with 25,334 multilayer ORM paths changed, cp_watson.xdet with 24,106 detail
    ranges, 3,775 ORM DDS). Misty's ground roughness median 0.282 -> 0.412 (CP2077 0.41). Record 39748 (an overpass)
    left as served: a stale bake from before its templates were fetched (the bake cache key omits its inputs).
    Renderer to score it on night-27.
    Scored: correct but small on screen (the decals carry their own roughness). The black wet patches were the
    porosity model (rough = fully porous, x0.2): H44 (02fc09dc) makes r_sceneWetPorosity "0.224096 0.787952 0.5 0.4"
    the default (lower-half gap at ref05/07/12 29.4 -> 29.7 at 0.5/0.4, 27.4 at 0.35/0.4; sheets/h38/por_retune.jpg);
    a look choice for the owner. Left: ref07's wet asphalt still mirrors neon (film 0.28 + SSR); ref05's lower half
    too bright even dry (lighting, the sidewalk's pale albedo).
  - **Night-28** (`build/cp_watson_dev-night-28`, kit28.py, 10:59) = night-27 + H44 + the mlrough overlay. Smoke:
    loads clean. Not installed.
  - **The real issue at Misty's** (owner: "need the real issue found and addressed before can know how to tweak";
    "mistys ground is enough"). Findings: the cameras of ref06/07/08/12 were never solved (only ref05 and ref09 are), so
    six-view gaps mixed different pictures; the wet model is a minor part (dry, ref05's lower half is still 73 vs 49.5);
    reflections at 3x (r_sceneReflect, fitted by eye) mirror facades on ref07's road (1x is physical); albedo checks out
    (asphalt ~0.08 linear). Root cause of the ground: its light. With fixed exposure the lights make it all; two nit
    tubes ({L_Mistys_Neon} 41%, {L_RedStreet_Top_Main} 23%) made the sidewalk cyan-grey (0.25 0.38 0.37) where CP2077's
    is warm (0.45 0.30 0.25); no cyan pool under Esoterica's neon in CP2077. Tested and rejected: tubes as line sources
    for surfaces, spot lumens into the cone, nit x0.05/x0.01. Nit-unit lights at 1/pi (luminance x area, no Lambertian
    pi): ref05 lower half hue 0.42 0.31 0.27 -> 0.51 0.27 0.21 (CP2077 0.51 0.30 0.20), brightness 69 -> 58 (49); the
    ring unchanged. Fixed in cp2gltf (uncommitted); overlay build cp_watson_scene-10j-nit-x0.3183. Open: ref05's upper
    half turns bluer (0.42 0.29 0.29 vs CP2077 0.53 0.27 0.19): another source there.
  - **Night-29** (`build/cp_watson_dev-night-29`, kit29.py, 11:24) = night-28 + the nit overlay. Not installed.
  - **Owner requests, 2026-11-06 ~05:00:** the wet ground's lost detail went to the renderer session (H31: CP2077's
    RainAreaSettings porosity 0.224-0.788 instead of one 0.2 roughness cap); Fable + Codex Astra round 3 over
    everything since round 2 (`look/review-20261106`: context, four briefs, renderer snapshot; areas scene, character
    loading, pipeline, engine and bike).

**State at 2026-11-05 ~15:45 (superseded where the block above disagrees).**

- **Resume here (handoff before compaction, 15:45).** Owner focus: the hill view ref04 (06:05) "until it's resolved",
  but "don't overfit... I'm going to change the time": a reference calibrates model constants, never per-file knobs.
  - **Night-19 (`build/cp_watson_dev-night-19`, the kit to show):** renderer H23 (`look/stream-20261104/dll/H23`,
    825224ce) and the packs zz_cp_watson_scene_zz_ads (build `-10j-ads2`), _zz_zfol (`-10j-fol1`, leaf flag 0x800),
    _zz_zholo (`-10j-holo1`), zz_cp_watson_scene_xscr (`-10j-xscr4`), zz_cp_watson_env_em (`cp_watson_env-em32-dawn3`).
    Every change is a line in its KIT.txt. Night-18 is untouched.
  - **Cameras:** CP2077's solved from the shots (`look/cp77ref-20261104/classes/solve_cam.py`: SIFT to a render's
    r_sceneDebug 13/14 position maps, PnP; cg_fov = the true horizontal FOV). ref04: `cams_ref04s_settled.txt` (wait
    900: settle before shooting); ref15 / ref14 (the hill at 00:11 / 00:12): `cams_hill_night_solved.txt`.
    Launch with `cp77ref-20261104/shoot.sh <name> <kit> <bat> <cams> [--pack ...]` (waits for a free client, BelowNormal,
    retries). Score with `classes/regions.py <shot> <render>` (region colours) and `classes/look_score.py`.
  - **Done today for ref04:** ads (far billboard proxies glow in their baked colours: flag 0x400; 71 vector holograms
    converted with dots, bloom and one calibration), day glows follow exposure (r_sceneEmissiveExposure 0, ref 1.3:
    the 32x is gone), the sun = CP2077's trajectory (latitude 35, summer) with its clock 1 h ahead of solar time
    (fitted on ref04's cast shadows; dawn: yaw 11.5, elev 26), foliage canopy shading (r_sceneFoliage 0.7) and day
    clouds (r_sceneClouds 0.35), SpeedTree FoliageProfile alpha cut (profiles in `<work>/fp`).
  - **Next (in order):** (1) generate the dawn / day / night .xenv from CP2077's master env 24 h curves
    (`look/cp77ref-20261104/dayenv/env_at.py`; JSON `cp77/env/json/cp2077_master_env_ep1_v006.env.json`) through ONE
    conversion (fog colour/density, ambient from skylight, exposure, sun colour/strength; sun from the model above),
    calibrated on ref04 AND ref15/ref14 together; the dawn file's hand values (haze 118 132 150 @ 0.0003, fill 3.0
    3.6 5.5, sun 130) are provisional, and the day file's 13:00 sun lacks the +1 h. Night gap: CP2077 dense teal
    haze, ours clear brown (Kabuki's); our road too wet. Dawn gaps: far haze / road too bright, fewer and smaller
    foreground pines than CP2077, overall less golden. (2) H23's temporary cvars r_sceneHoloGain / Curve stay 1 (the
    calibration is in data). (3) The ref15 / ref14 night look round once the night .xenv is generated.
  - **Renderer session** (local_1b203a4d..., idle): built H17-H23, uncommitted in wt-stream. Crash 2 (a heap block
    freed inside NVIDIA driver calls on load) is mitigated in H20+ but not reproduced (0 of 40). Owner: page heap OK
    for a *consistent* crash; the owner runs the reg commands we hand them (memory page-heap-allowed-for-crashes).
  - **Uncommitted here (commit only on the owner's word):** cp77mat.py (ads proxies 0x400, particles_hologram,
    FoliageProfile alpha cut, leaves 0x800, hologram dots record + mask squared + ColorMultiplier/50, screen data
    fixes), cp2gltf.py (glow_albedo normal alpha, flags 0x400 / 0x800, inst_rot), cp77_rotate.py (new), cp77_tiles.py,
    cp77_ads.py, README and docs. Patch scripts outside the repo: `<levelport root>/levelport/cp77/watson/`
    patch_ads18.py, patch_xscr19.py, patch_holo19.py, kit19.py.
  - **Disk:** C: 45 GB free after the owner's trash empty (10i scenes, run binaries, retain.py moved there). Cleanup
    before compaction: `.codex-trash/20261105-141730-coordinator-cleanup` (1.9 GB: look runs' game-module copies,
    look/patchcheck-20261105's sector extraction; MANIFEST.tsv) awaits the owner's empty; four orphaned priority-watcher
    loops of mine were stopped (launch through `shoot.sh`, which ends its watcher, never a bare `( ... ) &`).
    On the owner's yes, the old tile tags went to `.codex-trash/20261105-142812-old-tile-tags` (10g, 9 and the small
    test tags, ~51 GB unique); `tiles/10h` (current) and `tiles/10` (the kits' collision chunks) stay. C:'s drop today
    was data, not held files: tiles/10h 24 GB for 10i/10j, ~7 GB of 10j scenes, the look runs. Measure unique bytes
    with hard links counted once and reparse points skipped (look/p1b-p2-p10-20261103/tree holds a junction to
    GameData).


- **15:30:** night-19 = renderer H23 (825224ce: H20 crash 2 mitigation + H19 foliage canopy / day clouds + H21
  emissives following exposure + H22 hologram dots + H23 hologram bloom 0.3) with the packs zz_ads (ads2), zz_zfol
  (leaf flag 0x800), zz_zholo (`cp77/watson/patch_holo19.py`: holograms' masks squared before the mips,
  ColorMultiplier / 50 - one calibration on ref04 + ref15, now in cp77mat), xscr4 (`patch_xscr19.py`: hologram dot
  records) and env dawn3 (r_sceneEmissiveExposure 0, foliage 0.7, clouds 0.35). ref15 / ref14 (the hill at night)
  cameras solved (`cams_hill_night_solved.txt`). The samurai matches CP2077 at dawn and night
  (`sheets/solve/samurai_h23.jpg`). Next: the .xenv generated from the master env curves, calibrated on ref04 +
  ref15 (night haze: CP2077 dense teal, ours clear brown).

- **12:50, ref04 (the 06:05 hill; owner: stay on it, but "don't overfit... I'm going to change the time"):**
  CP2077's camera solved from the shot (`look/cp77ref-20261104/classes/solve_cam.py`: SIFT to our position maps,
  PnP, 205 inliers at 0.62 px; `cams_ref04s_plain.txt`: 48478 -22727 4784, yaw 215.47, pitch 3.61, cg_fov 100.5).
  The sun: CP2077's trajectory (latitude 35, summer) with its clock 1 h ahead of solar time, one constant from
  ref04's cast shadows (06:05 -> yaw 11.5, elev 26; the uncorrected 18.9 / 14.2 shaded the foreground). Night-19's
  dawn env is `build/cp_watson_env-em32-dawn2` (also haze 118 132 150 @ 0.0003, fill 3.0 3.6 5.5, sun 130,
  r_sceneEmissive 32: provisional, to be replaced by values generated from the master env's 24 h curves
  (`dayenv/env_at.py`) through one conversion, glows following exposure as CP2077's exposureScale does). Night-19's
  renderer is H20 (H17 + crash 2 mitigation; 0 of 20 crashed, as did H17). H19 (foliage canopy shading, day
  clouds, cvars off by default) is asked to be merged with H20 as H21. Converter: SpeedTree leaves take the
  FoliageProfile alpha cut (fetched to `<work>/fp`) and two-sided leaves set flag 0x800. Trash on C: 17 GB
  (10i moved on the owner's yes, run binaries, retain.py) awaiting the owner's empty.

- **08:45, ads (owner: "need to be vibrant and all of them need to be present"):** ref04's instance / material maps
  (`look/cp77ref-20261104/cams_ref04_ids.txt`: r_sceneDebug 15 / 16 into PNGs) show three classes. (1) Far billboards
  are CP2077's proxies (`metal_base_proxy.mt`, attach 6-170 m): a baked ad BaseColor with a grey Emissive mask at EV 5;
  344 of 390 billboard proxies converted as cutouts with no glow (the masked glow was opaque-only), the rest glowed
  white. (2) `particles_hologram.mt` (71: Greaser, the samurai, DTR, the kanji sign) had no branch: flat grey. (3) By
  day every glow was 1/32 of night's (dawn / day exposure 0.04, r_sceneEmissive 1). Fixed in cp77mat / cp2gltf
  (holograms: blend + glow mask in ColorParam; proxies: mask in the normal's alpha, flag 0x400 = glow in the baked
  colour, renderer H17 asked of the renderer session) and for night-18 without a rebuild: `cp77/watson/patch_ads18.py`
  -> `build/cp_watson_scene-10j-ads1` (1,411 records), `zz_cp_watson_env_em.pk3` (`build/cp_watson_env-em32`:
  r_sceneEmissive 32 by dawn and day), kit **night-19** (`kit19.py`, H15 until H17). On H15 the holograms show at
  16-32 (`sheets/n18/n19_ref04_emissive.jpg`). H17 (`stream-20261104/dll/H17`, e63872a0 = H15 + flag 0x400) built by
  the renderer session; night-19's bin64 now H17 (KIT.txt). Hill reshoot `sheets/n18/n19_hill_*.jpg`: the tall
  tower's purple and Greaser ads, the samurai, DTR and the kanji sign present day and night, the night layout close
  to ref15. Still short of ref04: NEW JOB less bright, the pink flower ad (a different screen there), clouds.
  ads2 (09:05, `patch_ads18.py ads2`, now in night-19): billboard proxies' masks scaled to full at their peak
  (cp77mat; a third peak at 128-160): next to no change at ref04 (NEW JOB's mask peaks above 128 in a few texels).
  CP2077 shows NEW JOB's proxy there too (it takes over at 498 m; the hill is ~540 m off): the gap is its contrast
  and the dawn haze, not the content.
  H16's load exits were crashes: (1) the .xscr / .xrot loaders read the file after freeing it (H15 too; fixed in H17
  and H18 = H16 + 0x400 + fix); (2) a heap fault uploading a small streamed DDS tail, not fixed, 1 of 2 H17 loads in
  the renderer session's checks (0 of 3 here): the renderer session is chasing it (page heap / symbols) for H19.

- **07:40:** night-18 ran in game beside another game (owner: untimed checks may run while they play;
  `look/cp77ref-20261104/runs_n18_gaming.sh`: hidden, nograb, BelowNormal; GPU ms still wait for no game).
  Rotation works: the ring (0.54% of pixels move between shots, 0 with `r_sceneRotate 0`) and the hill fan (0.49%,
  0.001% off); the ref05 fan (radius 23 at 1,300 u) shows no change. Compare round `sheets/n18` (CP2077 | night-17 |
  night-18): brightness as night-17 within 0.5 everywhere (the screen data changes pixels, not the exposure); the
  ads show their pictures (ref07's board, ref09's ring). Gaps unchanged: ref05 +55, ref06 +77, day +52 too bright;
  ref07 / ref11-12 centres 16-21 dark; dawn +21 / +4 the closest. ref10, ref13, ref14 still have no camera.
  H16 cell A/B (`sheets/n18/n18_h16_cell_ab.jpg`; cell0's first run exited at load, the rerun was fine): not
  readable (the first cameras looked at the placement points). Re-aimed 07:55 (`cams_n18_cell2.txt`, found by
  `cams_n18_cellprobe*.txt`; `sheets/n18/n18_h16_cell_ab2.jpg`): the arrows look the same either way (0.1% of
  pixels), so that spot no longer decides it; the street-plate ad lies flat, seen from above at r_sceneExposure 0.4:
  cell 0 shows several ads squeezed and stacked along the strip, cell 1 one ad filling it (enlarged). The owner's
  look at a street plate in CP2077 decides. H16 exited at load in 3 of 9 runs (no crash event; H15 0 of 8): tell
  the renderer before it goes into a kit. Foliage views ran (runs/n17_fol_*). The bike (`wishlist/bike-pmove` d0353eb2)
  is merged into this branch (2e3d981f).

- **Kits** (`build/`, none installed; each runs from its folder): **night-15** (tag 10's chunks, hub10 with El Coyote
  Cojo's spawn, scene-10-int-ocean, H6, `Play_Watson_Day.bat`; spawn check 0 of 19 stuck, collision unchanged since);
  **night-16** (scene `-10g2-int-ocean`: vector-board recolor, base\fx neons / holograms / glow cards, signage EV
  capped at 1, a placement's instance light in place of its template's; H12 = H6 + `r_sceneLightDedup 4` +
  `r_sceneFogLightMax 0.3`, the owner's picks; `r_ext_multisample 2`, owner; Day and `Play_Watson_Dawn.bat`, CP2077's
  06:05); **night-17** (scene `-10h-int-ocean`: + CP2077 foliage 1.54 M instances, coverage-preserving mips for
  cutouts, CP2077's own ad per screen; else as night-16).
- **night-18** (05:22, `kit18.py`): scene `-10j-int-ocean` = night-17's + the ad fixes (each screen's pick keyed by
  its place too: same-named stacked panels; `21_9`-style formats, 687 -> 484 screens unknown; an ad's `_Animated`
  record where one exists, ref05's Naranjita), `maps/cp_watson.xscr` (4,721 screens' CP2077 shader values) and
  `maps/cp_watson.xrot` (14,889 rotating fans, propellers and ad rings); renderer **H15** (a81aa691 = H14's .xscr
  flipbooks + .xrot, `r_sceneRotate 1`; the screen look behind `r_sceneScreenLook`, default 0; H12's defaults kept;
  uncommitted past wip/stream d09e5561). Day / Dawn / Chunks launchers as night-17. Not yet run in a game (the owner
  was in another game: gaming mode). Checks: `look/cp77ref-20261104/classes/check_xscr.py` and `check_xrot.py` (every record
  on an instance standing where a rotating node stands: 14,889 of 14,889). Patched 05:50 (`cp77/watson/patch_xscr18.py`,
  the renderer's night-18 check): `home/base/zz_cp_watson_scene_xscr.pk3` (build `-10j-xscr3`) overrides the .xscr -
  layers 2-4 relative to IntensityPerLayer X (L_0_emissive_downtown's 3 was X=3, Y=3; vehicle_ui's 11 is CP2077's own),
  tile counts as CP2077 writes them, fractions too (street_a_plate_b's ads_set_2x4 plays at 0.5 x 4, 0.2 fps: the
  renderer must not round; the cell formula is unverified, 1/w of the sheet assumed), 0 read as 1. cp77mat has both.
  Raw ScanlinesIntensity 2-20 on holograms stays raw (the renderer reads it as a gain or clamps).
  Renderer **H16** (`stream-20261104/dll/H16`, 401dc7d1; offline only, in no kit): unrounded cells, `r_sceneScreenCell`
  0 = cell 1/count of the sheet, 1 = count of the sheet (which one CP2077 does is open: the owner's look at
  vending_machine_j's arrows near Misty's and street_a_plate_b's ad, cams_n18_cell.txt), layer weights clamped 0..1,
  layer 4 drawn, grid darkness above 1 as a gain. A drop-in for night-18's bin64 after the A/B.
- **Rotation facts:** a worldRotatingMeshNode turns about the mesh's own origin (850 of 850 checked lie on the axis
  through the bounds centre); the nodeData record's `Pivot` is a streaming point, not the hub (1,362 nodes over 200 m
  off): never rotate about it. Axes Y / Z only in Watson, 2-6 s fans, 100 s ad rings, half reversed; the phase is
  hashed per node (CP2077 starts each as it streams in). `tools/levelport/cp77_rotate.py index` ->
  `<work>/rotate_index.json`, in the scene parts' key; parts carry `inst_rot`; the merge offsets it over the tiles'
  instances end to end (the renderer's index). 10i (`rebuild10i.sh`, run 3) and 10j (`rebuild10j.sh`) built.
- **Speed:** `cp77_tiles.py --start-gap` (default 10 s) - the 10 s pause after each start held 8 workers to 3-4 once
  tiles finish in under a minute; `--workers 12 --start-gap 2` built 267 parts in 26 min at Idle beside another game.
- **Next, in order:** (1) the renderer, once the owner is out: night-18 in a game (fans turning, flipbooks, the ad
  picks), H14/H15's A/B, night-17 frame timing and the downward grass views (`runs_n17_resume`), a compare round
  against the owner's shots. (2) `--screen-sheets` scenes (whole television_ad sheets, for H14+). (3) The open
  reference findings below.
- **CP2077 reference views** (the owner's 12 shots, RT off: `look/cp77ref-20261104/shots`; cameras, sheets
  `sheets/n16`, `sheets/n17` and findings in the shared notes `cp77ref-20261104.md`). Owner: fix each finding as a
  class, never a view. Still open from them: the multilayer bake loses the brick tint (grey facades), Misty's warm
  lamp perhaps off or quest-only in CP2077, the GOMORRAH sign drawn as paint, Kabuki's footbridge / HOTEL neon /
  steam, ref09's ad tower and moon, the hill's far ads and hologram, haze colour by district (brown vs CP2077's teal),
  8,838 non-world-channel lights (need the meshes' light channels), 484 ad screens without a format, 14 flipbooks at
  PlaySpeed 0, light_gradients / fog_laser / hologram.mt unconverted, 14 foliage meshes unfetched.
- **CP2077 updated 2026-11-04** (build 26161058; WolvenKit 9.0.1 cannot read its sectors). Owner: our sectors stay
  patch 2.31; new data only through WolvenKit 10.0.0-beta.1 (`<newmod tree>\rend2_comparison\build\tools\
  wolvenkit-10.0.0-beta.1`, beside 9.0.1): the ad TweakDB dump (`look/ads-20261105/ads.json`, `ads_dump.ps1`), the
  ad widgets, atlases and textures (`fetch_ads.sh`), their renders (`ink_render.py`, 1,766 layouts, `rendered/`).
  Foliage comes from the 2.31 sectors' embedded .cfoliage. `cp77_ads.py index` wrote `ad_index.json` (10,317
  screens: group, pin, format).
- **Commits (local, none pushed):** 30ada286 (this pipeline: area cells, the reference fixes, foliage, ads, tools,
  docs); renderer wip/stream 8b4928ed (H12) and d09e5561 (H13b), wip/water 4b40289a, wip/p3-viewcull1 29806dc3;
  collision/world128 5be30d23 and 5e63c2df, collision/m2 c5807d77. **Uncommitted since 30ada286:** cp77_ads.py (place
  in the key, formats, animated preference), cp77mat.py and cp2gltf.py (.xscr, `--screen-sheets`, .xrot), cp77_rotate.py
  (new), cp77_tiles.py (`--start-gap`, the rotation key); kit and build scripts live in `cp77/watson/` (kit10, kit16,
  kit17, kit18, rebuild10g/10h/10i/10j).
- **Trash for the owner:** the repo's `.codex-trash\20261105-superseded-test-scenes`. The owner emptied
  `<levelport root>\.codex-trash` 2026-11-05 ~04:50; C: had 36 GB free at 05:22 (10i's and 10j's scenes, about
  13 GB each, are on it; 10i is superseded by 10j).
- **Traps of 2026-11-04/05:** a kit script must copy, not link, the files it edits (kit16 wrote night-16's notes into
  night-15's KIT.txt and launchers through hard links; repaired); never edit converter code during a build; a new
  tiles tag must link `build/` too (the scene reads door_open there: 10g first drew 2,076 opened doors shut); a merge
  keeps the first part's record for a shared material key, so a partial rebuild cannot test a material change;
  long CPU jobs at normal priority sized to the machine unless the owner is gaming.

**State at 2026-11-04 ~11:15 (overridden by the 2026-11-05 lines above where they disagree).**

- **Night-14 played** (night-13 + scene `-9b-int-ocean7`): 24 /screenpos reports (3 noclip repeats). Reviewed with an
  offline picker (`tools/levelport/scene_pick.py`: the mesh and material under a report's pixel) and zfight.py.
  Converter fixes, in tag 10:
  - stand-in ads only from the advertising and quest billboard folders (cp77mat `AD_ROOTS`): the pool had taken
    `fx_steam_ventilation_03_8x8` (the cloud walls), a fire flipbook, `street_crossing_wait` (WALK/WAIT on billboards),
    curb strips and vending machines; a sheet read whole and as frames keeps the frames;
  - entity node scale dropped (`Entities.each`): CP2077 draws an entity at its components' scales. Its proxies say so
    both ways (street_lamp_cccpz_a 8 x 5.5 m against our 32 x 22; a node-0.23 billboard 3.5 x 4.6 against our 0.8 x
    1.1); 3,628 of 87,059 nodes carried a scale (the Japantown ring ad at 16, holo feeders, wall lamps);
  - lying pipes closed: `frame_split` takes a lintel and a sill from vertical lines (sewer_pipe_a_straight_l300_ext
    had two posts and nothing over or under; the owner fell in);
  - area **`night_city_b`**: night_city's cells, the 28 its outline encloses (water strips with their bridges: the
    Kabuki-Japantown bridge stopped at -1043, 1029) and El Coyote Cojo's block (12 cells, x -1536..-1024, y -1152..-768,
    north of the world-128 limit; radius 4,000 m: 3,200 cut the bar at 3,235 m).
- **Renderer (G5, `look/stream-20261104/dll/G5`)**: R1 character light, R3 haze near sampling, R4 light clusters in
  over-full grid cells (the bar's lights), R6 `r_sceneProxyPush` (proxies behind coplanar detail by their near: road
  `prx` against bended roads and `grid_prx`, 15,787 pairs in -7_1) on by default, 1.3 ms faster at 4K; R2b haze
  ceiling the owner's look choice, off. The 082706 hole (a tower over the water, x -1365, y 834, drawn only as
  stacked `wat_lch_building_b_v2_c` proxies): tag 9's cells lacked (-1408, 768), so night-14 kept only the top
  module (its origin in the next cell); night_city_b has the cell and tag 10 both lower modules (`scene_pick.py
  --every-range`, the renderer's instance list). Not z-fighting, draw ranges or the facade's alpha test (CP2077's
  AlphaThreshold 0.38: window holes over the proxy's Windows_0-3).
- **Detail slots (owner: 8, behind `r_sceneDetailSlots`)**: `.xdet` v2 (`cp2gltf.py scene --detail-slots 8`;
  `cp77_tiles.py --detail-slots`), 256 px share maps, raw shares; test scene `cp_watson_scene-10s` (tiles -7_4, -5_8,
  -5_3; `tag10s.sh`).
- **Tag 10** (`tag10a.sh`, `tag10b.sh`): part a done; part b's collision parts, scene parts, merge
  (`cp_watson_scene-10`), chunk glbs, then `tag10c_list.py` (tiles whose collision changed). The collision session's
  hub10 (a ring pocket around the bar's block to y -127,275) waits on part b.
- **Next playable's work order (night-15)**: tag 10's chunks and scene, hub10, G5; El Coyote Cojo's spawn; and
  (owner 2026-11-04) **a sunlit version beside the night one**: the renderer's day look (sun, sky, exposure, haze) and
  the scene's day values where CP2077 has them. R5 (buildings lighting their surroundings) waits on the owner's CP2077
  shots; the dock haze ("holyflashbang") on a CP2077 shot at (-1074, 464).
- Disk: the CP2077 Steam update stages on D: (`<Steam library>\steamapps\downloading\1091500`, 75.7 GB at 10:00);
  nothing of the level port stays on D:.

**State at 2026-11-04 ~04:30.**

- **Scene streaming works on the whole joined map** (renderer session, look/stream-20261104, notes
  renderer-scene-streaming.md; DLL dll/C5 be0fde86, uncommitted in wt-stream: grid 128, water2, R8, streaming B/C/D):
  night-2 loads in 24 s (A1, all resident: 130 s), GPU 6-8 ms median at nine of ten spots (A1 11.5-84; the new area's
  south 16.9), client VRAM 7.97 GiB, adapter peak 9.89 of 12 GiB (RTX 3080 12 GB). B: per-LOD residency over 179 k
  shared geometry blocks (shards are the wrong unit: 20,360 of 57,004 meshes draw geometry stored in other tiles'
  shards); C: images by the nearest wanted LOD; D: light-shadow faces redrawn as their casters arrive.
- **The converter writes the scene shards stored** (ZIP_STORED: the renderer range-reads them), in packs under 2 GB
  (zipfile writes no offset past 2^31 without zip64; SCENE_SHARD_PACK 1.9 GB). `cp_watson_scene-9b-st`: scene-9b
  repacked so (repack_stored.py).
- **Kit `build/cp_watson_dev-night-4`**: night-2 with scene-9b-st, the C5 DLL and `r_streamPoolMB 1536`; the renderer
  session's smoke first, then the owner's playtest.
- **Interiors** (owner: "look to cp2077 to see how they treat arasaka tower"): CP2077 loads an interior sector only
  while the camera is inside its descriptor's streaming box (all.streamingblock, category Interior; Arasaka's upper
  floors from z 128 m). By that rule, 90% of the interior placements within 1.5 km are hidden at street level by the
  tower, 98% at Misty's; -6_0 is 78% interior. `tools/levelport/tag_interiors.py`: slot bit 24 + group, maps/<map>.xint
  (v1 proposed, not yet agreed; C5 does not read it): `cp_watson_scene-9b-int`.
- **Bay** (renderer session's water check: drawn as background): the scan holds 29,018 water patches, about one per
  10 m cell over the bay, but the scene's water part took only the kept cells' (water_records' in_area): 15,642. Fixed:
  water_records takes every patch in the hub's ring box for the scene (AREAS 'half'). `tools/levelport/patch_water.py`
  wrote it into a built scene with 7,931 sea patches of 20 m besides (look/ocean-20261104/sea_mask.py: water on the
  Realistic Map reference, no patch, nothing collidable 0.3-6 m over sea level): `cp_watson_scene-9b-sea`, 36,949
  water instances; kit **`build/cp_watson_dev-night-7`** (night-6 with it), awaiting the renderer's smoke.
- **Interiors data**: `cp_watson_scene-9b-int` built (614,221 instances tagged, 4,919 groups; 422 interiors without a
  box); the renderer agreed the v1 contract, its reader is next (D3/C5 would read bit 24 as a signal index: no kit).
- **4K numbers (night-6, D3)**: GPU 10.6-13.6 ms median at nine spots (75-95 fps), the new area's south 19.6 (its haze:
  1,411 volumes past the 1,024 a group, 11.9 ms); the cull 2.1-2.3 ms everywhere. Renderer next: a tile-level coarse
  cull, haze volume binning, the interior reader. Owner's target 240 fps (4.2 ms). 16-bit light-shadow atlas chosen
  (owner, `r_sceneLightShadowDepth 16` in night-6's cfg).
- **Renderer E-series** (4K): E1 group cull (cull 2.0 -> 0.5 ms), E2 haze binning past 1,024 volumes (new south 19.6 ->
  7.3 ms), E3 the interior reader (a small win; the size test and LOD already dropped most interior draws): 8.8-10.7 ms
  at the busy spots, 93-136 fps. Kit **night-9** (E3, scene-9b-int-sea: interiors and the bay), smoke pending; night-7
  and night-8 superseded unsmoked. Owner (2026-11-04) on 240 fps at 4K: "do lossless and then small trade offs via
  cvars" (each trade-off its own cvar, off by default, measured with A/B shots; no upscaling).
- **night-9 failed its smoke on the bay** (gaps under piers: the sea mask's collision band; a lighter double-blended
  rectangle: 20 m sea patches over rotated scanned ones). **Kit night-10** (E3, scene-9b-int, no bay patch; E3 with those
  packs smoked at 4K) is the playable one. The bay goes to the renderer's ocean plane at sea level (drawn after the
  patches, depth LESS, an .xwat v2 record to define); `cp_watson_scene-9b-int-water` (9b-int with all 29,018 scanned
  patches, no 20 m fill) waits for that record.

**State at 2026-11-04 ~02:45.**

- **Tag 9 built; kit `build/cp_watson_dev-night-2`** (Watson and the next area as one map; `kit9.py`): dev-boxfix-b
  with tag 9's chunks (224, three packs split at 3.5 GB), scene `cp_watson_scene-9b`, the collision session's joined
  hub (`look/collision-20261030/hub9/tag9`: box ring +-76,875 x +-112,875 u, void kill, col-12's 18 spawns, light grid
  43,126 samples) and its world-128 binaries (`look/collision-20261030/world128/bin`; no water mode yet). The spawn
  check and a residency smoke are the collision session's next; then binaries with water2 (one tree), the renderer's
  water look check, and the owner's playtest. Not played yet.
  - **night-2 draws only the sky**: rd-vulkan joins every scene tile into one buffer and stops at 2 GB
    (vk_scene.cpp: the joined scene is 3.59 GB; Watson's geometry is 0.93 GB, the new area's 2.09). The whole map
    also passes the RTX 3080's 10 GB (Watson alone needs about 8.3 GB). night-3: night-2 with `cp_watson_scene-9b-cc`
    (cp2gltf `--scene-tiles`: the 56 tiles within 1,056 m of Arasaka Tower, 1.73 GB). Owner (2026-11-04): no disc;
    "make the renderer streaming work": per-tile scene residency, the renderer session's, plan and cost first.
  - Tag 9 tiles 263 (4.88 M placements, 2.6x Watson's; the 95 fringe tiles hold 1,068). Chunks compiled 210 (140
    with collision, 70 empty); -10_-11 (one piece at y -189,600 u, past the world limit and the ring) left out of
    night-2; night-1 had it (trash).
  - **The fetch dropped meshes silently**: part a's batches returned without exporting 552 meshes and nothing crashed
    (one alone exports fine); 702 meshes, 8,710 placements in 86 tiles (Arasaka Tower's -6_0: 1,495). Fixed: the fetch
    retries whatever a batch left out, eight a call. Repair runs tag9d-tag9g: the refetch, then the 86 tiles' parts
    again, the merge as scene-9b (scene-9, the first merge, stays as it was: numbered).
  - **A new area needs `fetch --embedded` and `--embedded-ml`**: 528 bended road meshes live inside the sectors
    (`localInplaceResource`, "Embedded"), in no archive; tag 8's recipe left the stages out because an earlier tag had
    run them. `stage_fetch_embedded` now carries on past WolvenKit's exit 3 (832 of 835 exported; 3 name test-kit
    textures). Left without a glb: 461 placements of 31 meshes (Watson's own 217 among them).
  - door_test skips boxes of a placement scaled to nothing (singular; fringe tile -10_1 crashed on one).
  - patch_kit drops a base kit's extra BSPs of a tile it builds again (chunk_<tile>_p under a whole compile).
  - Speed: below-normal and idle processes are throttled by Windows on the Balanced plan even on an idle machine (0.4 of
    a core a worker, 4 of 20 cores busy; at Normal 18 of 20). Part c ran five workers with the start stagger released
    at 45 s, then 10 s (tag9c_more.sh, tag9c_stagger45.sh); compile 23:39-02:36.
- Not committed: everything listed at ~17:30 and ~15:30, and tonight's fixes (fetch retry, embedded export, door_test,
  patch_kit drops, the water contract, generated UVs, lamp bulbs).

**State at 2026-11-03 ~17:30.**

- **Tag 9: Watson and the next area as one map** (owner: "do that and the new sectors in a pass"; WISHLIST 15, route
  A). AREAS `night_city` (cp2gltf.py): Watson's centre and frame kept (kits, spawns, patches stay valid), Watson's
  cells plus every cell of the owner's 78 tiles (`areas/night_city_cells.json`, 558 cells), Watson's work folder
  shared (`work_dir`), sectors from Watson's box and 10 boxes over the new tiles (`sector_boxes`; their bounding box
  is twice their area). The far edge is CP2077 y -768: 112.8 k u from the centre, so the engine limit goes to 128 k
  (the collision session: MAX_WORLD_COORD and its users, q3map2, a joined hub without Watson's box ring).
  - Runs, chained, Idle, each stopping under 25 GB free on C: (`cp77/watson/tag9a.sh`, `tag9b.sh`, `tag9c.sh`,
    `tag9ab.sh` chains them): a, blocks, sectors, scan, fetches, tiles tag 9; b, every tile's collision part (Watson's
    90 too, so every door uses the fixed floor test), every scene part, the merge (`cp_watson_scene-9`), every chunk
    glb; c, the chunk BSPs (`tag9c_list.py`: 210 of tag 9's 263 tiles) into D:
    `.codex-research/collision-tag9/build/cp_watson_colchunks-11`. Tag 9 tiles 263: Watson's 90, the 78 new, and 95
    fringe tiles (big pieces whose bounds centre falls outside the cells, 1,068 placements). Compiled: all but 53 of
    Watson's, the 30 with unmatched slabs (2,949) and 8 seam tiles whose non-proxy placements changed
    (-1_7, -2_6, -3_6, -4_3, -4_4, -4_5, -7_2, -9_2) compiling again; the 53 keep dev-boxfix-b's patched chunks. 44
    Watson tiles differ from tag 8 only in proxies and draw distances (`proxy_hide`), which never collide; the scene
    parts are all rebuilt anyway. `tiles/9/gaming` (a file) drops part c to one worker.
  - Part d, the kit, waits for the collision session's binaries and hub: dev-boxfix-b with the new chunks
    (`patch_kit.py --full-pack`, which now splits packs at 3.5 GB: the joined chunks pass 4 GB) and scene-9's packs
    in place of tag 8's and of the scene patch.
  - `cp77_tiles.py --only-tiles FILE`. Tag 8's placements are kept in `placements-tag8`; a `scan --area watson` would
    rewrite the shared store Watson-only (told to the dev-speed session).
- **col-15 render reports** (`look/render15-20261103/FINDINGS.md`): the hangar floor is terrain without UVs
  (`generated_uv` from UVGenScaleOffset, measured on 389 cell edges) and the work light's bulb a lights_interactive
  material that fell to metal_base unlit (186 materials, ~24,900 instances): both fixed in the converter and in tag 9.
  Water is a placeholder and the street's blur the bake resolution with one detail layer. Owner (2026-11-03):
  the renderer water mode (scrolling normals, Fresnel reflection without the rain gate, depth absorption; the
  renderer session, 2-3 days). Its data contract v1 is in the converter (scene-9 carries it): water materials kind
  blend, flag 0x80, a generated FFT ripple normal (water_ripple_normal; CP2077's nc_water.xbm is a mask), tint the
  appearance's ScatteringColor, and maps/<map>.xwat (WATER_XWAT, the renderer's mapping) with the foam. Streets: four detail
  slots proposed (2-3 days), not decided.
- Not committed: everything listed at ~15:30, and tag 9's tool changes.
- **Next:** parts a-c tonight; the kit when the collision session has the world limit and the hub; then the owner's
  playtest. R8 and R4 part 2 still wait for "done playing" (timing runs alone: part c pauses for them).

**State at 2026-11-03 ~15:30.**

- **col-15 playtest** (eight /screenpos reports; `look/col15-playtest-20261103/FINDINGS.md`): two collision bugs,
  both fixed in code and patched into a kit without compiling a tile again:
  - `stage_chunks` drew every box of one mesh in a tile with the tile's first box of that mesh: tag 8's split frames
    (posts and lintels) came out as whole slabs or as copies of one post (31,032 boxes in 64 tiles; the restroom
    doorway at -1430, 1039). The cache is now keyed by shape.
  - `door_test`'s floor check sampled boxes every 0.1 m and missed floor plates of no thickness: Vic's door and others
    shut "no floor". Now the segment against each box: 11 of the 32 open.
  - **`patch_chunks.py`** (new): switches the old boxes' brushes off in the base kit's compiled BSPs (a shader with no
    contents), compiles the right boxes alone into `chunk_<tile>_p`, re-decides doors from the stored parts and takes
    their leaves out of the scene (`zz_cp_watson_scene_patch.pk3`). Whole city in ~15 min at Idle (`--jobs 3`);
    `patch_kit.py --full-pack` writes the whole chunk pack (raw member copies). Kit
    **`build/cp_watson_dev-boxfix-b`** (col-15 + the patch): probe gives both reports open, the other six unchanged;
    tile -6_4's rays at the pieces: same as the exact truth 272 of 400 (col-15 143), missing 58 (176).
  - Left: 2,949 drawn-wrong boxes whose brush could not be matched (2,894 electric tower lattice parts, ~20 at doors)
    keep their slab until the next full compile; their right pieces are in. The holes (-1446, 1090 and -1448, 966)
    predate tag 8 (no ground placement in the scan); the render reports (hangar floor, water, lamp bulbs, low-res
    street) are not looked at yet.
- Not committed: the col-15 fixes, `patch_chunks.py`, `patch_kit.py --full-pack`, `probe.py`'s `_p` layers, round 2.
- **Next:** the owner plays dev-boxfix-b; then the R8 and R4 part 2 gates ("done playing"); then tag 9.

**State at 2026-11-03 ~08:30.**

- **Tag 8: the owner's col-14 playtest fixes** (six /screenpos reports; record
  `look/ep1-quest-block-20261102/FINDINGS.md`):
  - Interior sectors sit on a grid half the exterior's (32 m x 2^level, `SECTOR_M0_KIND`); tag 7 took the exterior
    grid, so most of Watson's interiors were never fetched. Now 881 interior sectors (was 386).
  - New meshes got no LOD copies (`fetch --lods` was missing from tag 7a; MeshLods reads `meshes_lod/`), so the plans
    dropped them as missing: 5,619 placements in tag 7, 22,955 in tag 8's first split, after the fix 218.
  - Phantom Liberty's own sectors are described in `default\ep1\blocks\all.streamingblock` (`EP1_BLOCK_REGEX`,
    `json_blocks/ep1q`); without it their q307 ending variants were all drawn (the Zetatech front over Misty's).
  - `stage_tiles` drops exact duplicates (7,061) and CP2077's lighting-only planes (`GI_ONLY_NAMES`, 1,655), and
    looks again at meshes cached without bounds.
  - Walk-through frames (door frames, sliding-door casings, checkpoints) collide as posts and a lintel around the
    opening (`frame_pieces`: 17,030 split); a single box filled their own doorway. Doors open 391 (tag 7: 225),
    closed 95 (247). `doorway_check.py` and `probe.py` show Misty's door and the checkpoint open.
  - Built: tiles/8 (1,902,208 placements), scene-8, 90 collision parts, the chunk pack
    (`.codex-research/collision-tag8/build/pack`, SHA256 8a252d4a..., 89 chunks + 1 empty, source assert 0
    mismatches, sky check passes). Kit col-15 (`build/cp_watson_hub_col-15`, `look/kit-col15/col15_kit.sh`): col-12's
    hub, the tag 8 pack, scene-8, col-14's binaries.
- **Dev-loop speedups** (owner approved all; `look/dev-speed-20261103/REPORT.md`, `IMPLEMENTATION.md`), each gated
  byte-identical against tag 8's parts or chunk maps: P1a (`mapio.cross3`), P11 (rgba lookup table), P5 (no pixels for
  cached DDS, tex LRU, cp77_tiles biggest-first and `--min-free-gb`), P6 (scan pool, incremental sectors, tiles kept),
  P4 (`stagekey.py`: stage-scoped part keys, per-tile doors, `--rebuild`), P3 (material result cache, work/matcache),
  P9 (walk LOD and frame pieces per mesh, work/meshcache), P8 (`probe.py`, `patch_kit.py`, `patch_tiles.py`).
  P1b (batched slab writer, `slabwriter.py`), P2 (conversion cache) and P10 go into tools after the tag 8 pack
  (installing now). `zfight.py`: coplanar overlap report per scene part, draw ranges respected; run on named tiles.
  Tag 8's key files are stale under the new keys: the next run rebuilds all 180 parts once.
- **Interiors study** (`look/cp77-interiors-20261103/NOTES.md`): tag 9 carries P1 (light channel volumes from the
  cooked outline buffer), the renderer's fields (slot bit 24 = interior, .xfog v2 with a channel box, .xprobe v1)
  for its P3 (interiors out of the sun cascades, two switches) and P4 fog; the interior map (.ximap) if cheap.
  Low priority: P5 (V's apartment's quest sectors), P7's conversion. Not done: P2, P6, P8.
- **Next area:** WISHLIST item 15 (78 tiles south and east, one map with Watson after the world limit is raised).
- **col-15 checks passed:** the hub spawn check (0 of 18 in solid, 18 floors, 89 chunks); the residency smoke (exit 0,
  0 violations, 0 false walls, 9 chunks / 902 MB preloaded, 17.8 s to chunks, spawn hold 0 ms, client peak private
  11.0 GB); walks through Misty's door and the checkpoint at CP2077 (-1467, 1046), both ways, all through. The first
  smoke failed on a missing SDL2.dll: retain.py had moved col-12's binaries although KEEP.txt listed it (its
  path-form entries never matched; fixed), and the kit script copied SDL2.dll from col-12 (now from col-14, and it
  stops on a failed copy). col-12 has lost its binaries and packs.
- **Dev loop round 2** (`look/dev-speed-20261103/REPORT2.md`; owner "go for it"): R2 (raw-copy pack on C:,
  `build/cp_watson_colpack-<tag>`, kits hard-link it; KEEP.txt `re:^cp_watson_colpack-`) and R3 (`jobguard.py`) in;
  R1, R5, R6 (dev-speed session) and R4 (collision session) in progress; R8 Stage 1 (shadow pre-render 43 s -> 1-3 s,
  renderer) approved. Smokes pass `r_sceneLightShadows 0` in their private home. Owner "Not now" (2026-11-03) on
  raising the shadow face cap (4,461 of 5,846 shadow-casting lights get none).
- **Next:** the owner's playtest of col-15; then tag 9.
  Nothing from 2026-11-02 evening on is committed.

**State at 2026-11-02 ~20:00.**

- **Kit col-14** (`<levelport root>\levelport\build\cp_watson_hub_col-14`, launch `Play_Watson_Chunks.bat`): the
  playable interiors kit, handed to the owner. col-12's hub (hard link), tag 7 pack 2 (SHA256 5117919e..., copied from
  `.codex-research/collision-tag7/build/pack`; the 40 changed tiles' BSP CRCs checked against their compiles, the
  other 49 equal to tag 5), scene-7 (hard links), binaries built from 36db208d's source
  (`<levelport root>\build_col13_x64`, worktree `.codex-research/wt-col13`). Not installed. Residency smoke (the
  collision session's): exit 0, 0 violations, 0 false walls, 9 chunks / 870 MB at map load, spawn hold 0 ms, 18/18
  spawns, client peak private 10.33 GB. Scripts: `look/kit-col13/col14_kit.sh`.
- **Kit col-13 is withdrawn** (trash, `-col13-bad-pack`): tag 7's first pack held tag 5's BSPs for every changed tile
  (`grep -qx "$t"` with tile ids that start with `-`). Never played. The pack script now asserts each source BSP.
- **Commits** on renderer/pbr-materials, none pushed: bfa46e8e (pipeline: interiors and doors, `--scratch`, chunk
  staging retention), f4924c32 (cgame bigfont), 36db208d (spawn preload + collision doc P6 correction).
- **Next:** the owner's playtest of col-14 (collect `/screenpos` reports); then the tag 7 level check (falls/holes
  in the interiors; needs a client, so only while the owner is not playing; `look/levelcheck-*`). Open, not
  requested: doors within ~2 m of a tile edge may stay shut; the ground-collision gap near CP (-1549, 1222)
  (gate_metal_fence_1); the venue spawns' 4-chunk respawn holds; wishlist 14 (spawn pinning); daytime
  (`look/cp77-lighting-20261101/NOTES.md`: CP2077's probe GI, ECG_Sky fills, 24 h curves already in cp77/env/json).
- **Trash for the owner to empty:** ~32 GB (stamps 20261102T051840-levelport-retain, 20261102T051828-pipeline-scratch,
  20261102T052110-collision-m2-m4-closed on C: and D:) plus the col-13 bad-pack stamp on C:.
- **Gaming mode** (the owner plays often): every job at Idle priority, no clients, no GPU, no timed runs; RAM guards
  suspend the newest worker under 6/10 GB free (`cp77/watson/ram_guard7*.ps1`, `suspend_tag.ps1`).

**State at 2026-11-02 ~09:30.**

- **Tag 7: interiors and doors** (the owner: "lets do interiors next?", "start interiors").
  - `cp2gltf.py sectors --sector-kinds interior` adds CP2077's `interior_*` sectors (Watson: 386, json now 2,991
    sectors); scan and fetches as usual (910 new meshes). Tiles tag 7: 1.73 M placements.
  - Doors (the owner: "decorative and lift closed, rest open if something behind them, decorative shutters closed"):
    door-family entity components carry `ent_template` and `ent_pos`; `door_leaf()` picks a candidate's leaf (the
    sliding panel, handles, roll-up and freight leaves); the collision build sets leaves aside and `door_test()` opens
    a door when both sides are clear for 0.6 m at waist height with a floor within 1.6 m, else the leaves collide as
    boxes. Decisions live in each tile part (`door_open`, `door_closed`); the scene drops the open doors' leaves
    (`door_open_keys`). Order: collision parts first (`cp77_tiles.py --build-only`), then scene parts (their key
    includes the doors). Watson: 225 open, 247 closed (196 no floor, 51 blocked). A door within ~2 m of a tile edge
    sees only its tile and may stay shut.
  - Built: 40 collision tiles changed (13 placements, 39 doors), 51 reused from tag 5; scene-7
    (`build/cp_watson_scene-7`, 1.71 M instances, door leaves verified out on -5_5); chunk glbs in tiles/7/chunks. The
    collision session compiles them (look/collision-20261030/tag7/tag7.sh) into a tag 7 pack; kit col-13 follows
    (col-12's hub, the tag 7 pack, scene-7, binaries from 3c53296e + the font fix + the spawn preload).
- **Collision memory:** M-2 instancing stopped (pass 5: traces +137%, saving measured 18.5%); M-4 bands stopped
  (the wall rule projects 14%). Kept: the spawn preload (`cm_chunkSpawnPreload 2000`, `sv_chunkFirstEntryNear 1`):
  map-load peak 0.87 GB (was 2.29), one player in play 1.85 GB; accepted with its respawn-hold median failed
  (1.33 s, set by spawn-set size). Spawn pinning is wishlist item 14.
- **Tools:** `gltf2map --scratch DIR` (one fixed staging folder a worker, packs byte-identical; the .map is written
  there directly, so it may be on another drive than --out); `retain.py` moves a chunk build's per-tile staging once
  its pack exists; levelcheck's client never takes the mouse (`in_mouse 0`, minimised, not activated); cgame no
  longer registers hud.menu's missing bigfont (the smoke's font warning).
- **Kit col-12** (`build/cp_watson_hub_col-12`, scene-6 with the mesh decals): smoke passes with the font fix.
- **Lessons:** a check client must not take the owner's mouse (nograb launcher); a field name already in use
  (`ent`, the signal lamps' entity position) was overwritten once, caught by an identity check on entity_meshes.json;
  stale `tail`/`grep` watchers piled up to 338 (1.1 GB): stop old monitors' processes after a chain ends.


**State at 2026-11-01 ~20:45 (newest; overrides everything below where they disagree). Read this first after a
compact.**

- **Commits** on renderer/pbr-materials, none pushed, in order:

  | Commit | What |
  |---|---|
  | 6e62ab6b | pipeline |
  | f847bd65 + b69539e8 | renderer review batch, ~1.5 ms a frame |
  | c99dbde8 | collision: P6, C-a..C-c, M-5 |
  | f7680b24 | shadow defaults |
  | 39446f37 | mine: scene_part crash fix, decal records, the --inst-split converter |
  | df3a37b3 + 3c53296e | R-c DBuffer decals + report 1n |

  Earlier today: 04db99fa, f810f4f4, 6b3ebcd7, de01a42d. The tracker for every Fable and Codex item is
  `look/review-20261101b/OPEN_ITEMS.md`.
- **Progress against the Fable/Codex audits** (both rounds; tracker `look/review-20261101b/OPEN_ITEMS.md`, statuses
  current at 20:45):
  - **Defects, all fixed and committed:**
    - pipeline P-a..P-p, all 16 (6e62ab6b);
    - renderer round 2 A1-A6 (f810f4f4) plus R-a and round 1's D5/D6/D8 (f847bd65), and R-c, the decals the door
      report led to (df3a37b3);
    - collision round 2 C1/C2/C3, zip64 and Codex's items (6b3ebcd7), then C-a/C-b/C-c (c99dbde8).
  - **Defects still open, all low:**
    - renderer D1 (coplanar ties across phases, parked) and D4 (the load peak: ~1.3 GB and ~10 s, a refactor);
    - collision C-d's one map_restart residual (ClientSpawn's direct ClientThink), and Fable's D7/D8 covered by
      C-a/C-b.
  - **Performance, built:**
    - F-4 light loads -0.48 ms, F-11 packing -0.10, F-6 light list -0.91: ~1.5 ms a frame on every view,
      bit-identical;
    - F-1 shadow LOD default (up to -0.45) and F-9 middle-cascade cache default (-0.9 to -1.3 while running);
    - collision M1-M3 (dense4 4.95 -> 3.22 GB, traces identical, 9% faster);
    - P6 respawn hold 3.9 s -> 0.75 s; M-5 safe, no gain measured.
  - **Performance, measured and stopped:** F-2 (far LOD floor), F-3 (fade in the cull), F-5 (distance size cull),
    F-7's tile skip, M-1 (referenced planes, 0.20 GB).
  - **Performance still open:**
    - M-2 instancing step 0 (est. ~1.3 GB, compiling now) and M-4 bands with it; M-3 triangle soup waits for
      step 0;
    - F-8 (CP2077 occluders into the pyramid, for the prepass spikes);
    - F-10 (cheaper distant materials);
    - F-7's finer clusters (~0.4 ms, 2 days);
    - R-c phase 2 (parallax, decal roughness).
- **Playable kit: `build/cp_watson_hub_col-11`** (Play_Watson_Chunks.bat), not installed.
  - x64 built from f7680b24 in a clean worktree; col-10's hub (1.5 km, 18 spawns), tag 5's chunk pack and scene-5.
  - Smoke SMOKE_PASS (chunks 21.9 s, peak 4.87 GB, no errors; the reallybigfont warning is gone). P6 in the kit:
    a 755 ms hold on 2 chunks.
  - Not yet in a kit: R-c (df3a37b3) and scene-6.
  - The owner hasn't played col-10 or col-11 yet ("I don't need to play now").
- **Waiting to start: scene-6** (`cp77/watson/scene6.sh`): tag 5's placements, the render scene only.
  - It brings decal records, the P-g near defaults, P-f/P-l, and the crash fix.
  - It starts when the collision session sends "compile done" for step 0; launch it at Idle with
    `cp77/watson/ram_guard5.ps1` (that guard watches tag5.log, so point it at scene6.log or watch RAM by hand).
  - Then a kit col-12 (scene-6 + binaries from df3a37b3), and the renderer's Watson-wide decal look.
- **Running: instancing step 0 (M-2) + bands (M-4).**
  - The converter split is done: tiles/5i, README there.
    - dense4: 129,623 instances, 15.81 M triangles moved, 18.10 M left, 2,294 prototypes.
    - 133 thin pieces lose sub-10 cm parts on their own grid.
    - Tag 5's parts predate the placed-L walk fix, so the same-build baseline is tiles/5i/chunks_base.
  - The collision session compiles base, rest and protos, then measures the projection against the 25% go bar
    (DESIGN-M2.md), and reports it. Go/no-go is the owner's to read.
- **Tag 5 levelcheck** (`look/levelcheck-watson-tag5`): falls 1,161 m2, void 1,407 m2 (col-4: 9,977 and 3,233).
  - The largest void (777 m2, game -20964 -37581) is a scene-5 proxy with near 0; scene-6's P-g fixes it.
  - Check two ~90 m2 clusters near the deck (-4301 -48695, -6095 -48546).
- **Disk:** the owner authorized emptying both .codex-trash folders once (20:14); D: 117 GB, C: 50 GB free.
  - gltf2map's per-chunk staging (~1.6 GB a dense tile) is the bloat source. The reused-scratch fix is on the
    collision session's list after step 0.
- **Renderer** (`local_1b203a4d`): idle; review list done.
  - Remaining options: D4 load peak (~1.3 GB, ~10 s load), F-7 finer clusters (~0.4 ms, 2 days), R-c phase 2
    (parallax, decal roughness).
  - Measured but dropped: F-2, F-3, F-5, the F-7 tile skip.
- **Slots protocol** (both sessions follow it):
  - one Watson client at a time (VRAM);
  - untimed runs alongside my Idle jobs;
  - timed runs alone: suspend my jobs with `scratchpad/suspend_tag.ps1 -Mode suspend -Pattern <regex>`,
    resume after their "done".
- **Lessons today:**
  - my committed P-f line crashed every scene build (plan holds tuples); the door scene caught it. Build a small
    scene after converter edits.
  - The first tag-5 changed-tile test counted the draw-only `stream` field and listed all 90 tiles; compare only
    what collision reads.
  - Don't change a check's pass rule after a failure (the smoke's font warning: left as it was, and it went away
    on its own).
  - Two dense scene tiles at once took RAM to 0.46 GB; ram_guard5.ps1 suspends the newest worker.


**State at 2026-11-01 ~08:15, newest first; it overrides the older lines below where they disagree.**

- **Running:**
  - the coarse-structure arms (`cp77/watson/arms-ct.sh`, log `arms-ct.log`) are **done** (08:22). On dense4's 34 tiles:
    ct2000 has 28.7 M triangles and ct1000 has 24.0 M, against tag 3's 33.7 M (-15% and -29%). Boxes and water are
    unchanged. Both `tiles/ct<cap>/chunks` folders went to the collision session at ~08:25 for its fixed gate
    (`look/collision-20261030/merge/GATE-coarse-structure.md`). **Both fail walls (pass 3), neither is taken; tag 3's
    collision stays.** Within 8 u: ct2000 98.34%, ct1000 97.45% (99% needed). Leaks: 0.67% and 1.12% (at most 0.1%).
    New blocks 0.31% and 0.37% (within 0.5%). Spawns pass on both. Dense4 memory 4.24 and 3.61 GB against 4.95. Some
    misses are whole walls, so a coarser LOD moves or drops wall faces. The collision session writes this up in its
    section 30 after the census. My levelcheck on the arms (`look/levelcheck-watson-arms/run.sh`, Idle priority)
    gives its per-tile view. The full packs are under `look/collision-20261030/merge/ct<cap>/`, the per-chunk
    staging (~65 GB, regenerable) under D:'s `.codex-research/collision-arms`, for the owner to trash after the
    write-up.
- **19:30, the review rounds worked through and committed** (the owner: "work through all the feedback items",
  then "Yes to all"). The tracker is `look/review-20261101b/OPEN_ITEMS.md`. Commits on renderer/pbr-materials, not
  pushed:
  - 6e62ab6b: the pipeline (tags 3-5, proxies, streaming caps, levels 0-7, round 2 fixes);
  - f847bd65 + b69539e8: the renderer's review batch, ~1.5 ms a frame on every view with identical images (F-4 light
    loads -0.48, packing -0.10, F-6 light list -0.91);
  - c99dbde8: collision (P6 respawn hold 734 ms against 3.9 s, its "pending stops" mark failed as fixed with the
    diagnosis; C-a..C-c; M-5);
  - f7680b24: the owner's shadow defaults, r_sceneShadowLod 0.25 and r_sunShadowMidCache 1 (-0.9 to -1.3 ms while
    running).
  - **Uncommitted (mine):**
    - the decal records for R-c (cp77mat mesh_decal*, cp2gltf flags 0x40, tint.a, window = DepthThreshold and
      NormalAlpha, normal and height images);
    - the scene_part `with_proxies` fix: the committed P-f line read plan tuples as dicts, so every scene build
      since 6e62ab6b crashed; the door scene proved the fix.
    Commit them after the instancing agent's edits to cp2gltf.py land, picking only these hunks.
  - **Running:**
    - kit col-11 (the collision session): x64 from f7680b24, col-10's hub, chunks and scene-5, smoke plus a P6
      respawn;
    - the renderer's R-c DBuffer decals (v6k): identity holds, the door looks as CP2077 draws it, on the door test
      scene build/cp_watson_scene-door (tile -6_4, 500 decals). The deterministic lit redo and the timing run after
      col-11;
    - instancing step 0: an agent writes tiles/5i/ (protos, inst lists, chunks split by band), then the collision
      session compiles and measures against the 25% bar.
  - **Tag 5 levelcheck** (`look/levelcheck-watson-tag5`, key t5c2/t5d2, the round 2 fixes): falls 1,161 m2 and void
    1,407 m2 (col-4: 9,977 and 3,233).
    - The largest void (777 m2 at game -20964 -37581) is a scene-5 proxy with near 0 drawn as a floor. P-g fixes
      that from the next scene build on.
    - Two ~90 m2 clusters sit near the crossroads deck (-4301 -48695, -6095 -48546): check them in col-11.
- **14:37, tag 5 done** (`tag5.sh`, collision resumed by `tag5b.sh`). Pieces are with the collision session for kit
  col-10.
  - **Scene:** `build/cp_watson_scene-5`, 91 tiles, 1,673,487 instances of 26,308 meshes, 41,409 lights;
    missing_mesh 180.
  - **Collision:** 51.9 M triangles and 835 k boxes. 39 tiles changed against tag 3 (`tiles/5/col_changed.txt`,
    draw-only fields ignored); the other 51 reuse tag 3's parts.
    - My first changed-tile test counted the new `stream` field and listed all 90. I caught it within a minute and
      tag 3's parts overwrote the few built by mistake.
  - **RAM:** at 13:50 the two densest scene tiles together took free memory to 0.46 GB. `ram_guard5.ps1` now
    suspends the newest worker under 1.2 GB.
  - **Collision engine, committed on the owner's word:** 6b3ebcd7 and de01a42d (C1, M1+M2, M3d, C2, C3, zip64,
    D4/D9/D10, Codex's items).
    - zip64: chunks past 2 GB of a pack never streamed on the worker; a 387 ms hitch, now 0.
    - C4: respawning into an evicted region holds 3.78 s on 9 chunks. A shorter hold (wait only for near chunks,
      ~0.5 s) is offered to the owner.
  - **Kit binaries:** col-10's x64 binaries were built from de01a42d in a clean worktree (`build_col10_x64`). They
    carry character loading, the picker, cg_xenAnims and f810f4f4's renderer; GameData has the packs.
  - **Next:** the collision session compiles the hub and the chunks, assembles col-10 and smokes it; then I run
    levelcheck on tag 5 against scene-5.
- **13:20, tag 5 running (started 12:40) and the next kit:**
  - **Tag 5 grew** to fix three playtest reports at once. It exports every streaming level, 0-7: the crossroads
    deck wlc_01_crossroad_02 and the tower cranes are level 5, and the export had taken 0-3. Level 4+ pieces belong
    to the area by their placed bounds' centre, since a deck's origin sat outside the kept cells.
    - Each placement draws to its hide distance capped by its record's streaming distance (UkFloat1,
      `draw_distance`). The black ad was a proxy shell over a poster whose wall outlived its 171 m.
    - No NPC statues.
    - So far: 2,605 sectors, the store at 2,089,862 placements (all carrying `stream`), 87,607 entity components
      (-617). 90 tiles: the deck is in tile -6_4, the cranes in -7_6.
  - **Renderer commits (owner's word):** 04db99fa (descriptor pool), f810f4f4 (A1-A6, F3 on by default), ccdd8a7c
    (report 1l). The DLL is `occ/dll/cf810f4f`.
    - Timing: F3 -0.14 ms. H1 only pays from 400 m up. LOD bias 0.5 saves -0.51 ms but costs detail; the
      shadow-only LOD look waits for the owner.
  - **Collision:** M3d passed its timing (-9%). Dense4 is 3.22 GB (-35%, traces identical). C2 and the Codex items
    pass; C3 (remote client) and C4 (respawn hold) are running.
  - **The owner (13:20): "Merge collision work and bring in the character loading and animations".**
    - The collision session commits its gated work on the moved tip: 20b3a586 holds the character-loading merge
      b63c9dbb and the cg_xenAnims merge, Install 62.
    - It then builds every x64 binary from that one commit, in a clean worktree, for tag 5's kit col-10. The Tifa
      pack zz_zz_xen_tifa_anims_v2.pk3 is in GameData, which the kit reads.
    - col-9 had none of it: binaries 970dbb8c, renderer 6b3a7b51.
- **Tag 5, prepared at 12:05 (now running, see above):** `cp77/watson/tag5.sh`. The owner: "drop the NPC statues", batched with the
  other converter fixes to avoid double work.
  - The scene rebuilds in full; its keys hash the converter, so every part rebuilds either way, ~2 h like tag 4.
  - Collision rebuilds only the tiles whose non-proxy placements differ from tag 3's (`tag5_changed.py`; 41 tiles
    hold statues). The others reuse tag 3's parts, and the collision session compiles the changed chunks.
  - In the converter, uncommitted:
    - `CHARACTER_MESHES` skips entity components under `base\characters\` (619 statues; 641 records in tag 3's
      tile placements, so collision has them too);
    - levelcheck's load_scene drops v5 proxies (near > 0) from the floor reference;
    - a hide-sidecar conflict keeps 0 ("never hides").
  - **Waiting on** the renderer's three playtest checks before launch: the ad z-fight, the floating masts (proxy at
    every distance), and the crossroads hole (proxies off, albedo, the crosshair's hit point). A fix any of them
    calls for goes into the same rebuild. Playtest triage: `look/review-20261101b/playtest_triage.md`.
  - Done meanwhile:
    - **Crash:** the renderer's v5e (WIP ca05cb6e) puts texture descriptors in overflow pools. The repro crashed v5d
      on the 6th switch and v5e survived. Not committed: the owner "accepted" the results, and the commit word is
      to be confirmed.
    - **Collision:** C1 (clear-on-wrap) passes. M1+M2 (4-byte sides, implicit axial bevels) is identical on every
      corpus, and dense4 drops from 4.95 to 3.61 GB (-27%).
    - **Instancing census:** dense4's structure is placed 27.6x over its unique meshes. M3 is next.
- **11:20 crash, round 2 review, the owner's order:**
  - **Crash:** the owner's col-9 playtest crashed at 11:20:41 with an access violation in nvoglv64.dll (reading
    0x108), called from rd-vulkan, ~15 s after three model switches (cp_songbird variants) and a respawn that
    streamed ~15 chunks. The dump is `build/cp_watson_hub_col-9/bin64/jampgame_crash.dmp`; the handler's "game DLL"
    label is wrong. Sent to the renderer to diagnose; the Release build has no PDB.
  - **Playtest reports:** nine screenpos reports in `build/cp_watson_hub_col-9/home/base/screenshots/screenpos/`
    (11:07-11:14), to triage: a floating satellite dish, low-res NPCs, animated ads, a black ad by angle, floating
    things near -22479 -25164, a hole in the ground, two with no description.
  - **Round 2 review:** `look/review-20261101b/REPORT.md` (Fable + Codex on the renderer, pipeline and collision).
    The owner approved its order ("Yes, that order"):
    1. Quick fixes: the renderer's phase-2 band line (vk_scene.cpp:5404); checkcount 16-bit (collision session);
       retain.py liveness (mine, **done**: a build a running command line names, or one holding a RUNNING file
       with a live pid, stays whole; KEEP.txt honoured; --keep-build beats --keep; tested on a temp tree, dry run
       clean).
    2. Exact collision repacking M1-M3 (collision session; est. dense4 4.66 to ~3.0 GB with identical traces).
    3. Renderer H1 (distance-weighted size cull) and F3 (EQUAL lit pass) in a short timing slot.
    4. Chunk failures and respawn holds C2-C4 (collision session).
  - **Checks:** my level checks stay off (the owner: "keep them off for now"). The arms' M4 is marked not run in
    section 30.
- **Playtest kit, 11:00: `build/cp_watson_hub_col-9`** (Play_Watson_Chunks.bat), not installed. Hub col-8 (worldspawn
  distanceCull 56250 = 1.5 km, the owner's pick; 18 spawns, corpus identical to col-6) + tag 3's colchunks-5 + scene-4
  + engine 970dbb8c + rd-vulkan 6b3a7b51 (v5d: v5 plus the cascade fix the owner approved, "add that too"). Residency
  smoke passes (chunks ready 15.8 s, 0 violations, peak 3.67 GB of chunks, working set 5.3 GB). The renderer measured
  col-8 as played: 88 fps at the spawn, 60 at a rooftop start. col-8 is superseded. **The owner plays next.** My two
  level checks (tag 3 drop at 54/89, the arms' compare) are stopped, resumable by key: restart both run.sh files
  hidden at Idle priority after the owner is out of the game. D:'s trash stamp 20261101T105530 (the arms' staging,
  54.5 GB) is the owner's to empty.
- **Renderer, 10:13:** v5 committed (d11bc509, report 1k 05b6ee05); the collision session has its DLL. The fade costs
  0.18-0.36 ms (mostly sun cascades casting mesh and proxy through the overlap; a fix, culling proxies at their exact
  near distance in shadows, is proposed, not built). A 1.5 km far plane costs 0.07-0.28 ms on the streets and
  +2.31 ms from 400 m up. **Asked the owner which far plane the playtest kit uses** (I recommended 1.5 km).
- **Disk, 08:30-10:00:** C: fell to 18 GB, the owner emptied the trash twice, and retain moved 40.6 GB (keeping
  hub_col-5 and scene-3 in use). C: is now ~95 GB. colchunks-5's 54 GB of staging stays as the newest build's; the
  proposal of a reused scratch folder for chunk compiles (keep only the packs) waits for the owner.
  - the tag 3 per-tile checks: `look/levelcheck-watson-tag3/run.sh`, `run.out`. Resumable; the compare was paused
    twice and resumed. Compare its falls + void with col-4's 9,977 + 3,233 m2 when done. The compare is done; against
    col-4 it has missing surfaces 596,908 (col-4 1,129,833, -47%) and pre-drop fall-through clusters 49,026 m2
    (col-4 110,338, -56%); invisible solid is unchanged at 413,768 m2. drop-tiles is running.
- **Done today:**
  - **Tag 3,** with every playtest fix and no height cap (400 m): colchunks-5, hub col-6 with the named spawns
    (Misty's and the Afterlife), kit `build/cp_watson_hub_col-6`. Tested; not installed.
  - **Tag 4 = tag 3 + CP2077's proxies:** scene `build/cp_watson_scene-4` (v5: instance near distance in the flags
    word; 1,672,325 instances). Collision is tag 3's.
- **Renderer** (`local_1b203a4d`): its v5 loader, near cull and 40 m dithered crossfade are built on private branch
  wip/v5 (1b8c3fe9). Now gating on scene-4: images, then the cascades, then a timing slot (ask me first; pause my
  CPU jobs). Then commit and send the DLL to the collision session for the owner's playtest kit (tag 3 collision +
  scene-4 + the proxies' renderer). **The owner plays after the far-city proxies.**
- **Collision** (`local_d10492f0`): waiting for the arms. Bands designed and shelved (section 28). Structure
  brushes: the existing merge loses nothing, and the 0.1 m vertex grid stays (no grid gave 2.25x the brushes on
  -4_7; section 29).
- **Mine, uncommitted** (commit only when the owner asks), beyond the list below:
  - proxies: `proxy_rec`, `proxy_placements`, `fetch --proxies`, tiles `--proxies`, scene v5, entity hide from
    entity proxies;
  - named spawns (AREAS 'named_spawns');
  - the spawn checks limited to the spawn area;
  - `cp77_tiles.py --scene-only`;
  - `scan` keeping `prefab`.
- **Lessons recorded:**
  - wait for a session's own "done", not for a pid;
  - stop or pause your own CPU jobs during others' timed runs;
  - Windows Python text files need `newline='\n'` when bash reads them;
  - never put cleanup in a chained command (a chained `rm -rf` removed two scratch arm folders: regenerable copies,
    but against the trash rule).
- **Owner decisions today:**
  - keep the 9,600 u load radius;
  - no collision height cap;
  - distanceCull 20000 as a stopgap until the proxies;
  - proxies after tag 3, and the playtest after them;
  - doors and shutters animated as CP2077 does it, built with the interiors;
  - commit the collision engine work (done) and both visible renderer fixes (done).


**Read this first.** Sections 1-6 are the scope as reviewed; 7-13 are phase M, water, checks, tiles and disk; 14 is the
chunked-city decision; **15 is the plan review and everything since, newest at its end** (2026-11-01: the reviews, the
playtest, hide distances, the cap, distance and the commits). The owner is present (Remote Control). Decisions taken
under delegation are recorded in section 15 as such.

### Where things are

- **Disk:** the level port lives at `<levelport root>` (`.codex-research/levelport` is a junction to it). Never use
  `%LOCALAPPDATA%` for durable data (the packaged app redirects it). Its trash is `<levelport root>\.codex-trash`;
  the owner emptied it on 2026-11-01. C: had 135 GB free.
- **Committed on renderer/pbr-materials (not pushed, nothing installed since Install 50):**
  - collision: 970dbb8c (engine: residency, the review fixes, unloaded-as-solid, spatial index, compact structures),
    e346e186 (collision doc, colbsp, colmerge, q3map2 patches), e5a2d566 (gltf2map chunk options);
  - renderer: 9e2138b6 (review fixes), c8e44a43 (light clusters 1(a)), 6d1b0b37 (lamp-shadow rain), 767feff7
    (normals; owner approved after side-by-sides), cfd7bd3f (scene v4 + hide distances), 4d03d059 (distant-light
    stand-ins), e0782c0a (report 1j), plus the earlier order and volume fixes (cf415962, e6837414).
- **Uncommitted, this session's (commit only when the owner asks):**
  - `cp2gltf.py`: hide distances (scan, parts `inst_hide`, `scene-hide`, scene v4), the playtest fixes (embedded
    lamp templates via `write_embedded_templates`, skinned components, NumberPlate UI skip), `--col-walk-lod`,
    `--col-cutout-large`, tiled scenes, fetch identity;
  - `cp77mat.py`: the SpeedTree DiffuseMap fix, the `real_ad` filter, `neon_parallax_image`;
  - `cp77_tiles.py`: BUILD_ARGS with xc and `--col-height 400`;
  - `levelcheck.py`: chunk sets (`BspLumps` over several pk3s with cm_loadchunk offsets), per-tile compare and drop,
    streaming, empty-set guards;
  - `retain.py`: three review fixes;
  - docs: this one, `levelport-scene.md` (v4 and tiled), `INTEGRATION.md`.
- **Builds:**
  - collision: tag 2 chunks with hub col-2, cap 16 m;
  - playtest kit: `build/cp_watson_hub_col-4` (`Play_Watson_Chunks.bat`, distanceCull 6000 key, so
    `r_distanceCull 20000` works); `hub_col-1` is do-not-play;
  - scenes: `cp_watson_scene-1` (v3) and `cp_watson_scene-2` (v4 with hide distances; image packs hard-linked);
  - slice: `cp_watson_slice_scene-22` (v4).
- **In flight: tag 3** (`cp77/watson/tag3.sh`, `tag3.log`; parts in `tiles/3/`, `run.log`):
  - all 89 tiles rebuilt with every playtest fix, native hide distances and no collision cap (400 m);
  - then scene-3 tiled and the chunks;
  - started 01:58, about 1.5-2 h; the ad pool is now 37.
  No timing runs while it builds (the other sessions know).

### Sessions (this one manages them)

- **Collision** (`local_d10492f0`, `docs/levelport-collision.md` sections 16-26): committed. Next, when tag 3's chunks
  are written: colchunks for tag 3, a hub (`--worldspawn distanceCull=20000` as the default now), a kit, and the
  tag 3 resident cost.
- **Renderer** (`local_1b203a4d`, `docs/levelport-render-district.md` sections 1-1j). Pending:
  - after tag 3: the distant_c re-time and the SSR 2-ray, ~8% blend timing for the owner's floor dither;
  - fix the view-cull volume half's dropped haze cone before its gate;
  - the corner-window rooms (readback);
  - the reflection-history dropout.

### Next, in order

1. When tag 3 finishes:
   - check `tag3.log` (ALL_DONE), the parts' summary (`tiles/3/summary.md`), scene-3 and the chunks;
   - tell the collision session to compile colchunks and the hub, then the kit;
   - run the per-tile compare and drop on hub + pack (`levelcheck.py compare-tiles <hub>.pk3,<pack>.pk3
     build/cp_watson_scene-3/... <out>/collide --tiles cp77/watson/tiles/3/tiles.json`).
2. **CP2077's far-city proxies** (owner: "after tag 3"):
   - fetch the proxy meshes and sector levels 4-6;
   - match proxies to their nodes (sourcePrefabHash, ownerGlobalId);
   - the renderer crossfades in the overlap band, and the far plane moves to about 1.5 km.
   The distanceCull 20000 is a stopgap (owner: "kind of a meme until we get cp2077 distance meshes").
3. **The owner's next playtest comes after the far-city meshes** (owner, 2026-11-01): a kit with tag 3 and the
   proxies. They file /screenpos; reports land in the kit's `home/base/screenshots/screenpos/index.txt`.
4. **Interiors** (phase 4): Misty's, Viktor's and the Afterlife first; all in `json_keep`. Spots are in
   `look/review-20261101/playtest_triage.md`.
   **Doors and shutters open, built with the interiors** (owner, 2026-11-01: animate them "as cp2077 does it, with
   it rolling up"):
   - fetch each door's rig and its open and close clips (the gate entity's animation component), and convert them
     to a Ghoul2 GLM and GLA on the character-conversion path;
   - the game: a door entity with that model plays CP2077's clip on +use; its collision brush moves in step with the
     clip;
   - the door's static scene instance and chunk collision are left out.
   Watson has 84 roll-up shutters plus ordinary doors; the hub allows 512 models.
5. **Open converter items:** capsule (tube) lights become one point (glow beside a sign; the axis is unknown); a
   covered cargo box collides as its bounding box; `walk_level` ignores placement transforms (review); a tiled export
   isn't atomic; levelcheck `--engine` with chunk sets; the compare grid from visible floors only; the gltf2map stale
   chunk reuse (review C).
6. Collision bands later (CP2077's deco collision only near the player).

Reports: `look/review-20261101/` holds `REPORT.md` (the reviews' ranked plan), `cp2077_city_tech.md`,
`playtest_triage.md` and the five review reports. Hide-distance work: `look/hide-20261101/`. The cap measurement:
`cp77/watson/cap-2.log`.

### Tools (all committed unless noted)

| What | Where |
|---|---|
| One area, every stage in order, then the checks | `tools/levelport/cp77_area.sh <area> <tag>` |
| Spawns, aerial holes, collision compare, clusters, drops | `tools/levelport/levelcheck.py` (`spawns`, `aerial`, `holes`, `compare`, `collide`, `drop`); 64-bit client staged in `look/levelcheck-bin64/` |
| The engine's own trace for `drop --engine` | `levelcheck_trace` (`codemp/game/g_levelcheck.c`), a jampgame-only x64 build in `%LOCALAPPDATA%/xenjk_build_x64_levelcheck` (cmake `-A x64 -DBuildMPGame=ON`, the rest off; target `jampgamex86_64`) |
| Reference map (top-down render of the city, the owner's Realistic Map 32K mod) | `.codex-research/levelport/look/realistic-map-20261030/` (`ref_crop.py`, `our_mosaic.py`; local only) |
| Patched q3map2 (CullSides sweep, plane hash: map-22 1,250 s -> 23 s) | `tools/levelport/q3map2/` (patches, README); binary `../Xenmap/.tools/netradiant-custom-20260114-xenjk/` |
| The sector WolvenKit refuses | `tools/levelport/wk_read_lenient.ps1` (used by `cp2gltf.py sectors`) |
| 4K view timing on a slice or Kabuki, 32/64-bit | `.codex-research/levelport/look/watson-m-x64/slice_views.py --area kabuki` (or `watson_slice`) (research, not committed) |
| Kabuki signal look and sound check | `.codex-research/levelport/look/afterimage/spin_check.py` / `spin_check_slow.py` (600 s stall limit) |
| District survey, CP2077 world study | `.codex-research/levelport/look/watson-scope-20261030/` (`summary.json`, `m/summary_m.json`); `json_keep/` now holds the interiors only (phase 4); its exteriors are in `cp77/watson/json` |
| Start a work folder from others' caches by hard link (section 12) | `tools/levelport/seed_work.py <dst> <src>...` (dry run; `--apply`) (uncommitted) |
| An area through tiles: parts, merges, map, checks, per-tile summary | `tools/levelport/cp77_tiles.py <area> <tag>` (uncommitted) |
| Disk retention: superseded builds, their look-run links and old glbs to `.codex-trash` (section 13) | `tools/levelport/retain.py` (dry run; `--apply`) (uncommitted) |

### Traps that cost time on 2026-10-30

- **Stage order:** `fetch --ents` before `fetch`, and `fetch --embedded` always. The recipe does both; hand chains
  forgot them twice.
- **Sector names:** WolvenKit's serialise flattens folders: Phantom Liberty's `default\ep1\` sectors (same names as
  base) overwrote base ones, and another world's `exterior_-1_1_-1_3` replaced Night City's. Fixed in cp2gltf
  (`ep1-<name>`, the `03_night_city` regex); check for this in any new archive or area.
- **Heredocs eat backslashes:** `\b`, `\a`, `\n` in Python inside Bash heredocs became control characters in a doc
  and in a .ps1. Write such text with the editor or a script file; `file` reporting "overstriking" means it happened.
- **Shared machine:** other sessions run look worlds back to back. Install scripts wait for a gap (10 s works);
  timing runs wait for a quiet minute and for no q3map2/WolvenKit/blender. The bench's 180 s stall limit trips when
  the CPU is busy (use `spin_check_slow.py`). Detached chains: `Start-Process` Git Bash with a script file (Bash
  background tasks stop at 10 minutes).
- **Install numbers:** other sessions take numbers too (46 and 48 today); read INTEGRATION.md and the install log
  just before claiming one.
- **Far plane:** a map without a worldspawn `distanceCull` draws to 10,392 u (`tr_main.cpp` R_SetFarClip). The aerial
  check from 10,000 u lost everything under game z -392 (the canals' water); it now uses a copy of the map with the
  key set.
- **JKA content flags are not Quake 3's:** `CONTENTS_PLAYERCLIP` is 0x10, water 0x4 (`codemp/game/surfaceflags.h`;
  `q3bsp.CONTENTS` holds Quake 3's). The checks used 0x10000 until 2026-10-30.
- **Clients from an agent's shell:** renderer_bench's hidden new console hangs before the client logs; levelcheck
  starts clients with no console and output to `home/stdout.txt`. Give them absolute home paths (the 64-bit client
  runs from its bin folder).
- **Trash:** `.codex-trash/20261030T0623-levelport-superseded/` (13.8 GB of superseded sector JSON and compile test
  copies), `.codex-trash/20261030T2002-levelport-superseded/` (46.9 GB, section 13) and the gamedata-pre-* folders;
  only the owner empties it. D: had 124 GB free at 20:00.
- **Moving a pack frees nothing while a look run links it:** look runs hard-link the packs they test. Shed with
  `tools/levelport/retain.py`, which moves every link to a moved file.

## 1. What one block costs today (measured)

The Kabuki block is a 384 m square (CP centre -1185, 2052; meshes fetched within 135 m).

| | Kabuki block |
|---|---|
| Streaming sectors (levels 0-3) | 101, 1.2 GB serialised JSON |
| Render scene (scene-23) | 65,569 instances of 3,879 meshes, 4.9 M triangles at LOD 0 (6.0 M all LODs), 4,623 materials, 2,070 lights, 61 signal heads |
| Scene pack | 936 MB: 3,172 DDS images 832 MB, the `.xscn` 259 MB |
| Gameplay map (map-22) | 240,836 brushes in the BSP (20,993 world + 219,884 mesh brushes before merging), 23.6 MB pack |
| Warm rebuild, every stage (fixes-22 + signals-23, 2026-10-29) | scan 17 s, fetch 2 min (all cached), embedded 2 min, scene 11-18 min, collision build 15 min, map compile 18.6 min (`-bsp` 15.7), checks 17 min: about 70 min |
| Cold scene stage | about 25 min |

The map compile is the stage that does not scale: q3map2 `-bsp` time grows close to the square of the brush count
(`look/compile_scaling.py`: 47 k brushes 19 s, 94 k 113 s, 378 k 1,674 s).

## 2. How big Watson is

Measured from the game data (survey agent, `.codex-research/levelport/look/watson-scope-20261030/`, `summary.json`;
every kept sector serialised, not sampled). Distances are CP metres.

- **Borders.** The game's own: each subdistrict is a `worldLocationAreaNode` with a `worldLocationAreaNotifier`
  district ID, its real outline in `AreaShapeOutline.buffer` (the JSON's `points` is a placeholder square), in the
  level-6 sectors `exterior_-1_0_-1_6` and `exterior_-1_0_0_6`. Watson has no polygon of its own; it is the union of
  its four (`location_areas.json`).

  | Subdistrict | Area | Placements per km² |
  |---|---|---|
  | Kabuki | 0.49 km² | 743 k |
  | Little China | 1.09 km² | 459 k |
  | Northside | 3.02 km² | 230 k |
  | Arasaka Waterfront (left out) | 1.61 km² | 113 k |

  The game's Kabuki is a thin strip; most of our "Kabuki block" west of the market is officially Little China.
- **Kept area.** Kabuki, Little China and Northside: 4.58 km². Dropping the near-empty 128 m cells (under 300
  placements: open water north-west of Northside and west of Little China, the Northside east embankment, the
  Kabuki canal) leaves **4.03 km², 2,048 x 2,944 m**, centre (-1280, 2240): it fits the engine's 3,495 m with 723 m
  to spare each side across and 275 m along. Dropping them does not shrink the box (the south row and the north
  strip both have content). Heights -33.5 to 356 m once 29,783 placements below -100 m (hidden quest and cyberspace
  rooms, down to -827 m) are left out. Map: `density_grid_128.png`.
- **Size against the block.**

  | | Kabuki block (box) | Watson kept | Ratio |
  |---|---|---|---|
  | Exterior sectors, levels 0-3 | 101 | 1,699 | 17x |
  | Serialised JSON | 1.2 GB | 12.3 GB | 10x |
  | Mesh placements | 96 k | 1.59 M | 16.5x |
  | Unique meshes | 3,879 (in the scene) | 9,970 | 2.6x |

  The world repeats its meshes: 16x the placements need under 3x the unique meshes. Extrapolating Kabuki's averages
  (62 KB of geometry and 204 KB of textures a unique mesh, the pack's DDS stored uncompressed so pack size is GPU
  size; 64 B an instance), the scene's own buffers come to about 2.8 GB (0.6 geometry, 2.0 textures, 0.1 instances).
  That is not the whole budget (both reviews): the per-view visible lists are sized by instances times LOD parts for
  8 views x 2 frames (about 0.5 GB at district size unless capped), the lamp-shadow atlas built for every lamp would be
  about 2.3 GB, then render targets, reflection history, haze, cascades, characters and the level 4-6 proxies (not yet
  counted). Depot meshes are also not scene meshes (appearances, bends and component masks split them). Fitting a
  12 GB card (the owner's RTX 3080 is the 12 GB model) needs a real inventory and capped lists; an estimate, not a
  result. Phase M's inventory (section 7) roughly doubles it.
- **Collision is the big number** (measured on the installed map-22, phase M; corrects the review's 4.28 M sides,
  which divided the lump by 8 B, not JKA's 12 B): 240,883 brushes carry 2.85 M brush sides (11.8 a brush, 53% of
  them bevels q3map2 adds) and 2.30 M planes, 78.6 MB on disk, 165 brushes a leaf: 312 B a brush on disk, 331 B in
  memory on x86, 433 B on x64. Scaled 16.5x: a 1.2 GB BSP. The BSP format's lump offsets are `int` and the file is
  read whole, so it tops out under 2 GB; the client renderer copies the planes too, at twice the count (`tr_bsp.cpp`
  R_LoadPlanes, 382 B a brush: more than the collision itself), about 1.5 GB at district size for planes it never
  draws. Planes, not sides, are the lever (section 7).
- **Disk.** The Kabuki work folder is 11 GB (4.3 GB baked material images, 1.6 GB meshes with their LODs, 1.2 GB
  sector JSON); Watson's would be about 35-50 GB with its 12.3 GB of sector JSON. D: has 235 GB free.
- **Interiors.** 503 interior sectors (a 32 m grid, not the exteriors' 64 m) in the kept cells: 228 k placements,
  7,087 unique meshes of which 2,459 appear in no exterior; about 280 are furnished rooms (V's megabuilding,
  Afterlife, Lizzie's, Misty's, Totentanz, Judy's apartment, the No-Tell Motel, gig locations).
- **Data cost is small.** Unbundling every Watson sector took 16 s; serialising runs about 0.26 s a sector on all
  cores (a full exterior re-run about 7 minutes), the scan 0.1-0.2 s a sector. One sector, `exterior_-17_21_0_0`
  (the Little China / Kabuki edge), fails to serialise in WolvenKit 9.0.1 (an RTTI error on
  `AccessPointControllerPS.accessPointSkillChecks`); it needs a workaround.
- **Far detail exists.** The game ships exterior levels 4-6 (large proxies of whole blocks), which our converter
  never reads; they are the natural far-distance stand-ins.

## 3. What breaks at district size

| Limit | Where | Today | At Watson size |
|---|---|---|---|
| World size | `MAX_WORLD_COORD` (q_shared.h): +/-65,536 u | the block is 14,400 u across | +/-1,747 m from the origin at 37.5 u/m, 3,495 m across: the kept area must fit, centred |
| Process memory | `xenjk.x86.exe` is 32-bit, large-address-aware: 4 GB of address space | the scene's `.xscn` is read whole (259 MB), images load through the engine | a whole-district scene cannot be held at once; streaming, or a 64-bit build |
| Image slots | `MAX_DRAWIMAGES` 4,096 (tr_local.h), scene images share it | 3,172 used by the scene | scene images need their own pool |
| Pack and file size | `FS_ReadFile` returns `long` (32-bit on Windows, also on x64), BSP lumps and `Z_Malloc` take `int`; pk3 reads share one zip handle and seeking reopens | 936 MB pack, 246 MB `.xscn` | every file and allocation under 2 GB, containers under 4 GB, chunks separately addressable |
| Character lighting | the renderer's light grid from q3map2 `-light` (LIGHTGRID, LIGHTARRAY lumps) | 68 s | at 64 u spacing over the district about 148 M points, 1.5 GB; dropped, characters get ambient only until the renderer lights them from the scene |
| Entities and snapshots | 1,024 entities, 256 in each snapshot, 512 submodels and models, 256 sounds, 16,000 gamestate characters; no vis lump means every entity is offered to every client | 17 entities | fine for the exterior; doors, items and speakers (interiors) need a vis lump or budgets |
| Bot navigation | Bot2 graph caps: 32,768 cells, 16,384 nodes, 131,072 edges (bot2_graph.c) | per map | a district needs regional graphs, if bots play Watson at all |
| Per-frame culling | one GPU thread per instance per view, no occlusion (scene_cull.comp) | 65 k instances | 1.59 M per view: tile-level rejection and occlusion needed for long streets |
| Sun cascades | the far cascade spans the scene | 384 m | 3.5 km at the same resolution is 9x coarser texels: another cascade or a cached far map |
| Light grid | `SCENE_GRID_MAX` 64 cells a side, 48 lights a cell (vk_scene.cpp) | 225 u cells | 2,048 u (55 m) cells over 3.5 km: lights dropped in dense streets |
| Signals | `SCENE_MAX_SIGNALS` 256; cg_signals.c reads 64 KB | 61 heads | likely over a thousand |
| Rain mask, lamp-shadow atlas | one 306 m top-down layer; every lamp's shadow faces built at load (144 MB, 0.9 s) | fits the block | need tiling or streaming with the scene |
| Map compile | q3map2 `-bsp` was close to quadratic: `CullSides` tested every pair of world brushes, and the plane lookup hashed only `(int)abs(dist) & 8191` | 15.7 min for 241 k brushes | **solved in phase M**: both patched, 23 s for map-22, about linear (section 7) |
| Collision memory | every brush, side and plane resident (cm_load), plus the renderer's plane copy; a listen server shares the collision | 241 k brushes, 2.85 M sides | at today's 433 B a brush on x64, about 1.7 GB resident plus the renderer's plane copy and the 1.2 GB file at load: 64-bit, fewer planes, no renderer copy |

## 4. The work

The district is a data problem the converter already solves block by block; what does not carry over is building
everything as one job, the map compiler's cost and output size, and a renderer that loads and culls one scene whole.
Revised after two independent reviews (section 6): **measure first**, then four pieces, each gated on the Kabuki
block and on a Watson stress slice before it runs on the district.

### Phase M: measurements (each a day or less, before the phases are scoped in detail)

- **64-bit Kabuki.** Build the engine, modules and rd-vulkan as x86_64 and play Kabuki on it: frame time, load time,
  peak RAM and VRAM against the 32-bit build. Every later gate measures on the build that ships.
- **The q3map2 plane hash.** One compile of map-22 with a proper plane hash in q3map2 (netradiant-custom, source
  public). If `-bsp` drops from 16 minutes to a few, the quadratic term was the hash and phase 2 can be per-tile
  q3map2 compiles plus a lump merge; if not, our own writer.
- **Bytes a brush.** Sides and planes a brush on map-22 by brush kind (prop box, mesh slab, hull), and how many edge
  bevels a slab really needs; the target for whichever compiler wins.
- **Inventory.** Scene meshes, materials, images, lights and collision sides for the kept area as the converter would
  emit them (not depot counts), and the level 4-6 proxies: how many, how big, whether they cover the blocks cleanly.
- **Stress slice.** A dense Watson intersection plus a long street with its surroundings, converted with today's
  tools as a second scene: frame time (median, p95, p99), stalls, peak memory, cold and warm load, at the owner's 4K
  settings. The 120 fps target (8.3 ms) is gated here, early, not after the district is built.
- **The failing sector** (`exterior_-17_21_0_0`, Kabuki / Little China edge): a WolvenKit workaround or a newer build.

### Phase 1: one process for many tiles (pipeline)

- **Tiles.** Split the kept box into 256 m tiles (8 x 12, about 90 after empty ones), each built by itself:
  sectors to placements, render scene chunk, collision. Placements are assigned to tiles by their whole transformed
  bounds, not their origin; objects crossing a tile edge belong to one tile, and the converter's block-edge walls
  (the boundary boxes a single block build adds) must not appear between tiles.
- **Shared libraries.** Meshes, materials and images built once into a library keyed by what they depend on (depot
  path, appearance, settings, tool versions) and shared by every tile; outputs published atomically, so an interrupted
  build leaves nothing half-written.
- **Placements store.** One indexed store (SQLite or per-tile arrays) instead of a 1.2 GB `placements.json` per area;
  the 12 GB of sector JSON is scanned once.
- **Parallel, safely.** Tiles in bounded worker processes; first remove what workers would share (the mesh export
  edits WolvenKit's settings file for its run, `cp2gltf.py` `lods_unfiltered`).
- **Checks per tile, automatic.** Spawn clearance, the collision compare, the playtest-report spots and a fixed set
  of look shots after every tile build; seams between neighbouring tiles; a summary page of each tile's state.
- Gate: the Kabuki block rebuilt through tiles matches today's map-22 and scene-23; two neighbouring tiles join with
  no seam; an interrupted build resumes.

### Phase 2: the collision map at district size (faster compiles)

- The compiler chosen by phase M: q3map2 per tile with a fixed plane hash and a merge step (keeps its bevelling), or
  our own collision BSP writer (a kd-tree over the brushes, the bevels it needs, shared quantised planes). Either way
  it emits what the engine reads, not only collision: shaders with content flags, model 0 with the world bounds,
  every brush's first six sides axial in the order -X, +X, -Y, +Y, -Z, +Z (`CM_BoundBrush` reads bounds from them),
  nodes, leaves, leaf-brush lists, entities, the sky shell's few surfaces, and empty but valid surface, fog and
  lightmap lumps.
- **A vis lump with cluster = tile**, the tiles in range of each other visible: the engine then stops offering every
  entity to every client, which interiors will need.
- **Character lighting** without q3map2's light grid: renderer work (characters lit from the scene's lights and
  probes), scheduled in phase 3; until then a coarse grid.
- **The renderer stops copying the gameplay map's planes and nodes** for a scene map (it draws nothing from them).
- Gate on Kabuki: the same results as map-22 in the collision compare, contents and start-solid checks, swept boxes
  through the owner's report spots; bytes a brush and planes a brush at the phase M target; server trace time no
  worse. Then the stress slice.

### Phase 3: the renderer at district size (streaming, loading, long streets)

- **Load without holding.** Read the scene in separately addressable chunks straight to the GPU through a bounded
  staging buffer, not the whole file plus a full-size staging copy; scene images in their own pool; per-file and
  per-allocation sizes under 2 GB; parallel readers each with their own pack handle.
- **Cull by tile, then instance.** Reject tiles first, then instances; distant tiles drawn as the game's level 4-6
  proxies (a tile's levels 0-3 hidden when its proxy shows); occlusion against last frame's depth for long streets.
  Visible lists and command buffers sized to a budget, not to every instance's parts in every view.
- **Resident first, measured.** Keep the district's scene buffers resident while the inventory says they fit, the
  lamp-shadow atlas local to the camera from the start; texture-mip residency only if measurements need it.
- **Local grids and lists.** The light grid around the camera, rebinned as it moves, with per-cluster candidate
  lists and checked index capacity (the cluster shader packs 16-bit indices and tests every scene light); lamp shadows
  for the nearest lamps; the rain mask per tile; another sun cascade or a cached far map; signal slots and the cgame's
  signal file without fixed caps; characters lit from the scene.
- **Load time.** A number agreed first (cold and warm) and measured; uploads off the main thread.
- Gate: Kabuki frame time unchanged; the stress slice, then the district, at parity with Kabuki today: about 120 fps
  at 4K at the owner's settings, with p95/p99 and stall limits, including views down the long streets.

### Phase 4: the district, then interiors

- Build every tile; walk the district with the play tools (`/screenpos`, the lap video tool) and fix by tile.
- Interiors (503 sectors, 228 k placements) as a later pass: enterable rooms with doors, so door entities, submodels
  and sounds counted against the engine's budgets (section 3) first, and their own collision checks.

## 5. Owner decisions

1. **64-bit play build for Watson.** Recommended, and decided in phase M: a separate 64-bit exe and modules launched
   by a `Play_Watson.bat`, the 32-bit install untouched. The district's collision (1.6-2 GB resident at today's
   sides a brush) plus the scene's load buffers do not fit 4 GB; 64-bit removes that, though it does not lift the
   2 GB file and lump limits. The engine builds as x86_64 (`CMakeLists.txt`); every module we load is ours. The cost:
   a second build to keep, collision structures 25% larger, and movement or Bot2 results measured on one build do not
   carry to the other without a check.
2. **What to leave out.** Recommended: Arasaka Waterfront (as asked), the near-empty water and embankment cells and
   the hidden rooms below -100 m; keep the sparse Northside yards (they are on the way between places).
3. **Interiors now or later.** Recommended later (phase 4), after the exterior plays well.
4. **Frame-time target: decided** (owner, 2026-10-30): "parity to what we currently have, which is around 120fps
   at 4k". The district at the owner's settings must hold Kabuki's current frame rate, about 120 fps at 4K; gated on
   the stress slice in phase M and again in phase 3.
5. **Bots: decided, none in Watson** (owner, 2026-10-30: "i agree no bots for watson"). Bot2's graph caps
   (section 3) would make a district graph its own project.

## 6. Reviews (2026-10-30)

Owner: "run your plan by fable and codex astra". Both read the draft and the code, read-only; their full texts are
in `.codex-research/levelport/look/watson-scope-20261030/consult/` (`astra.md`: Codex `gpt-6-astra`, high; `fable.md`:
Claude Fable). They agreed on the direction (tiles, keep brush collision, resident first only as a measured
simplification) and on the main corrections, all taken above:

- **Measure before building** (both): the 64-bit run, the q3map2 plane hash (Fable), bytes a brush, an inventory and
  a stress slice gated at 120 fps early (Astra), each before the phases are scoped in detail.
- **The collision numbers** (Fable, from the installed map): the district's BSP against the format's 2 GB, and the
  renderer's double plane copy. Its side count (17.7 a brush) was a lump-size slip; phase M measured 11.8 (section 7).
- **The BSP contract** (both): shaders, model 0, axial first sides, the sky shell and empty lumps; the light grid
  lights characters, so dropping it is renderer work.
- **The GPU budget** (both): visible lists, the lamp atlas, targets and proxies are outside the 2.8 GB of scene
  buffers; depot meshes are not scene meshes.
- **Engine capacity** (both): snapshot, entity, model and sound limits; a vis lump with cluster = tile (Fable); Bot2's
  graph caps (Astra).
- **Pipeline safety** (Astra): WolvenKit's shared settings, dependency-keyed caches, atomic outputs, tile seams and
  bounds-based tile assignment.
- **Long streets** (both): flat per-instance culling with no occlusion; tile rejection, proxies and occlusion
  against last frame's depth; the far cascade at district size.

Checked in code before taking them: `CM_BoundBrush` reads bounds from sides 0-5 (cm_load.cpp:231),
`MAX_SNAPSHOT_ENTITIES` 256 (qcommon.h:123), the Bot2 graph caps (bot2_graph.c:26-28), `R_LoadPlanes` allocating
twice the plane count (tr_bsp.cpp:2398), a light-grid array mismatch dropping the grid (tr_bsp.cpp:2630).

## 7. Phase M results (2026-10-30)

**Map compile: solved** (agent, `.codex-research/levelport/look/watson-m-collision-20261030/`, README and
`results.json`). q3map2 built from the local netradiant-custom source (be3b369) matches the shipped binary lump for
lump. Profiling the unmodified build (38 stack samples) put 32 in `ClipSidesIntoTree` / `CullSides`, which tested
every pair of world brushes (about 2.9e10 pairs; 4.07 M overlap), and 5 in `FindFloatPlane` (the 8,192-bucket
plane hash, which only costs the .map load: 141 s, 12 s patched). Two patches (`plane_hash.patch`,
`cullsides_sweep.patch`: a sweep along x visiting only overlapping pairs, in the original order):

| q3map2 `-bsp -meta`, map-22 (240,883 brushes), 4 threads, machine about 70% busy | seconds |
|---|---|
| shipped | 1,250 |
| built from source, unmodified | 1,240 |
| plane hash patched | 1,068 |
| plane hash + CullSides sweep | **22.7** |

Patched scaling is about linear: 60 k brushes 5.9 s, 120 k 9.5 s, 241 k 22.7 s, 482 k 40.9 s (map-22 plus a copy).
Output: the sweep is exact (every lump identical); the hash patch moves 6 of 2,850,604 sides to an existing plane
within q3map2's epsilon (a tie-break); runs repeat exactly. So phase 2 keeps q3map2 (its bevelling, its tree) for
the whole district in one compile of minutes; per-tile compiles and a lump merge are not needed for speed. Kabuki
itself drops from a 16-minute compile to under half a minute.

**Size is the remaining collision problem.** Bevels are 53% of the sides; a linear program per bevel shows they are
needed (dropping one wrongly blocks a player box by a median 3.45 u on triangle slabs, 0.70 u on rotated boxes), so
keep the exact set. The savings are in planes: snap box-bevel distances outward to 1 u (182 k plane pairs to 21 k,
a brush grows under 1 u at one vertex), store only the orientations the sides use (1.26 M of 2.30 M; about 1.11 M
with the snap; the engine's collision does not use `planeNum ^ 1` for sides), and have the renderer skip the
gameplay map's planes. About 230 B a brush on disk then; the district's BSP well under the 2 GB format limit.

**64-bit costs nothing that matters** (`.codex-research/levelport/look/watson-m-x64/`, `timedemo_bits.py`, Kabuki's
lap demo at 3840x2160, both builds of 3b14e1c2 interleaved 64, 32, 64, 32 on a quiet machine). The 64-bit build
compiled clean (engine, game, cgame, ui, rd-vulkan).

| | 64-bit | 32-bit |
|---|---|---|
| GPU frame median, p95 | 8.28, 9.89-10.00 ms | 8.16, 9.77-9.78 ms |
| Timedemo | 113.5 fps (the first, cold run 100.6) | 113.8 fps |
| Peak private bytes, working set | 3.9 GB, 0.81-0.87 GB | 0.81-0.92 GB, 0.61 GB |

About +0.12 ms of GPU a frame, the same in both pairs. The 64-bit client commits about 3 GB more private memory for
the same scene, not yet explained (the hunk and zone sizes, the driver's mappings or the scene's staging; to check
before the district build).

**How the game manages its world** (survey agent, `.codex-research/levelport/look/watson-scope-20261030/m/`,
`summary_m.json`, from all 26,318 streaming-block descriptors and the Watson sectors):

- **Levels are distance bands, not copies.** Each level doubles the cell (64 m exteriors, 32 m interiors) and the
  request distance: level 0 within about 110 m, 1 about 190 m, 2 about 420 m, 3 about 860 m, 4 about 1.1 km, 5 about
  3.1 km, 6 the city. Small things live low (level 0: static meshes, instanced props, decals, entities, lights), big
  things and proxies high. Our tile residency can follow the same bands.
- **Quest variants.** Sectors split their records into named ranges switched on or off by default; 455 of Watson's
  1,081 level-0 sectors have them. Our converter keeps every range: 49,304 placements, 2,347 lights and 2,815
  entities in the kept area are off in the game by default (prologue and road blockades, cyberspace scenes, quest
  props, bodies). In the Kabuki block 787 placements and 177 entities: the converter should keep the default ranges
  only, and Kabuki's open "standing on nothing" lump is worth checking against them.
- **Far stand-ins exist and cover everything.** Levels 4-6 are proxy meshes (2,382 unique in Watson, 3.06 M triangles,
  simple materials) with a near distance where full detail takes over (level 4 buildings about 190 m) and a far hide
  distance (1.3-1.5 km); level 4 alone covers all 246 kept 128 m cells. Levels 1-3 hold HLOD proxies too. The data
  says how many nodes a proxy replaces, not which, so the renderer switches whole tiles by distance.
- **Occluders exist.** About 12,500 occluder boxes and planes over the kept area (`worldStaticOccluderMeshNode`,
  `worldInstancedOccluderNode`; a unit box scaled, or a two-sided plane), up to 60 m at level 4: ready-made culling
  for long streets.
- **The game's own collision is simpler.** `worldCollisionNode` compiled data decodes: the Kabuki box has 61,454
  actors with 82,012 shapes (33 k boxes, 41 k triangle meshes, 4 k convex hulls, 3 k capsules) against our 241 k
  brushes from render meshes; Watson 1.57 M shapes. Triangle meshes live in a PhysX geometry cache whose cooked blobs
  the agent parsed (14.3 M triangles city-wide), but only 78% of Watson's references resolve so far (WolvenKit's own
  lookup rule is not yet reproduced). Collision comes in three tiers (critical, normal, deco). Whether the game's
  shapes beat ours for brush count and walkability is a measurement on the Kabuki block, not yet made.
- **The failing sector is read.** WolvenKit's own DLLs loaded from PowerShell 7 with its parse errors marked handled
  (`m/scripts/wk_read_lenient.ps1`) give the same JSON on a good sector; the failing one holds 7,288 placements.

**Inventory, as our converter would emit the kept area** (emulating the scene stage; it reproduces the Kabuki scene
within 5%): about 1.64 M instances, 22,700 scene meshes (depots split by appearance; Kabuki 3,879), 28 M triangles
over all LODs (Kabuki 6.0 M), 23,600 materials, about 16,300 images, **about 4.2 GB of images** (twice section 2's
guess), 40,400 lights, 34,519 entities, 1,495 traffic signals (the renderer's cap is 256). With geometry about 1.2 GB,
the scene alone is about 5.5 GB before lists, targets and shadows: whole-district residency on a 12 GB card is
marginal, so texture residency moves from "if needed" to planned.

## 8. What phase M changes

- **Compile:** keep q3map2, patched (section 7); wire the patched build into gltf2map; collision size work (planes,
  the renderer's copy) instead of a new compiler.
- **Converter first fixes, useful for Kabuki today:** default quest ranges only; the failing-sector reader.
- **Collision source:** measure the game's own shapes against our render-mesh brushes on the Kabuki block (brush
  count, sides, walkability with the existing collision compare and report spots).
- **Renderer:** tiles switched by distance with the game's level 4-6 proxies, the game's occluders for culling,
  texture residency planned from the start (about 4.2 GB of images), signal slots past 256.
- **64-bit:** the play build for Watson; explain its extra 3 GB of committed memory first.

## 9. The stress slice (2026-10-30)

Owner: "wire the patched q3map2 into gltf2map, keep only default quest ranges, rebuild Kabuki, then build the Watson
stress slice and time it at 4K".

**Converter changes** (cp2gltf.py, gltf2map.py, tools/levelport/q3map2/):

- gltf2map uses the patched q3map2 (`$Q3MAP2`, else `../Xenmap/.tools/netradiant-custom-20260114-xenjk/`, else the
  shipped one); the patches and how to build them are in `tools/levelport/q3map2/`.
- `blocks` stage: the city's streaming index (`_compiled\default\blocks\all.streamingblock`, base and ep1); `scan`
  keeps only the default variant ranges. Kabuki: 96,109 -> 95,463 placements, 2,155 -> 2,007 entities (prologue
  holotapes, corpses and loot, bloody rags, quest GPS paths, road barriers, alternate-state cables and trash); the
  slice: 35,665 records left out.
- Three bugs the larger area exposed: Phantom Liberty's same-named sectors under `default\ep1\` (they stream beside
  the base ones) were flattened over them, now kept as `ep1-<name>`; the sector regex matched other worlds
  (`04_main_menu`'s `exterior_-1_1_-1_3` had replaced Night City's); one WolvenKit crash (0xC0000005 in
  UncookBuffers) stopped the whole fetch, now a crashed batch is retried eight meshes a call.
- The sector WolvenKit refuses (`exterior_-17_21_0_0`) is read by `tools/levelport/wk_read_lenient.ps1`.

**Kabuki rebuilt** (`cp77/kabuki/quest-24.sh`): scene-24 (65,088 instances, 2,154 lights) and map-23 (127 s to
compile in all, map-22 took 1,116 s); spawns in collision 0 of 16; the collision compare at the owner's report spots
matches map-22 (514,929 matched, 31,556 missing). The "standing on nothing 1" lump is still there: it is default
content, not a quest state. Not installed.

**The slice** (`cp77/watson_slice/slice-1.sh`; area `watson_slice`: centre -1344, 1472, a 640 m circle in a 768 m
box, the survey's densest square, across the Kabuki / Little China border): 564 sectors, 491,519 placements;
scene 1.33 GB, 266,643 instances (4.1x Kabuki's) of 8,441 meshes, 8.95 M triangles at LOD 0, 8,206 materials, 4,699
images, 8,879 lights; gameplay map 92 MB, 770,828 mesh brushes, `-bsp` 118 s, 475 s in all. Build times: scene
24 min (the Python process peaked at 12.7 GB of RAM), collision glb 34 min. Installed as Install 50
(`Play_Watson_Slice.bat`).

**Timing** (`look/watson-m-x64/slice_views.py`, the owner's openjk.cfg at 3840x2160, vsync and cap off, the player
dropped onto the street and looking along it, 5 s a view, GPU medians; Kabuki map-23 / scene-24 against the slice,
interleaved, two runs each on 64-bit, then the slice once on 32-bit):

| View | Kabuki (fps) | Slice (fps) |
|---|---|---|
| Centre / densest intersection, four ways | 140-198 | 80-180 (south 80-84) |
| West to east | 95-102 | 90-101 |
| East to west | 92-99 | **61-69** |
| South to north | 94-102 | **67-73** |
| North to south | 97-105 | 141-142 |
| Load | 17-33 s | 23-30 s |
| Peak private memory, 64-bit / 32-bit | 4.2 GB / - | 5.9 GB / 1.9 GB |

The slice loads and runs on both builds (the image slots held: 4,699 scene images); the 32-bit client needs 1.9 GB
for it. The worst long views are about 35% slower than Kabuki's worst (61-73 fps against 92-105), short of the
owner's parity target: that is phase 3's tile culling, the game's far proxies and occluders, measured against these
views. The 64-bit client's extra committed memory (4.2 / 5.9 GB against 1.9) is still unexplained.

**Next:** the spawn check on the slice (single-threaded: over 20 minutes on 770 k brushes; parallelise or run it per
tile), the scene stage's memory (12.7 GB for a quarter of the district: per-tile in phase 1), then phase 1.

## 10. Level checks (2026-10-30)

Owner: "you need to find a way to check for holes, bad spawns, collision issues yourself. do just an aerial view and
you should see plenty of holes in the ground where you can see the brown skybox underneath ... the size and frequency
of current issues is too much for human intervention".

`tools/levelport/levelcheck.py`, run by the recipe `tools/levelport/cp77_area.sh` after every build:

- **spawns**: each spawn's player box against the player-solid brushes with the engine's own plane test (bounds from
  the first six axial sides, as `CM_BoundBrush`), plus a floor within 64 u. 2 s on 896 k brushes (the old check:
  20+ minutes); self-test: a box at the centre of 200 random brushes found inside each.
- **aerial / holes**: the installed client photographs the level straight down from 270 m in 4K tiles with the sky
  cleared to magenta (`r_fastsky 7`), scene materials flat (`r_sceneDebug 1`, no haze) and the finest LODs
  (`r_sceneLodBias 64`); magenta inside the level, projected to the ground, clustered on 0.5 m cells. A 40 m band
  inside the boundary is left out (placements are kept by origin, so pieces straddling it are missing by design). About
  a minute.
- **collide**: the collision compare's grid (`look/kabuki_collide.py`) clustered into visible floors with no collision
  (fall through) and collision with nothing visible on it (invisible solids).

What it found on the slice, and the fixes (all converter or recipe faults, not the game's data):

| Build | Interior holes | Cause fixed |
|---|---|---|
| slice-1 | 462, 21,964 m2 | - |
| slice-2 | 33, 598 m2 | `fetch --embedded` was never run: the roads and terrain the sectors embed (971 + 33 placements) missing from scene and collision |
| slice-3 | 31, 595 m2 | `fetch` ran before `fetch --ents`: 7,102 entity placements (traffic lights, crossings, signs, doors) had geometry but no materials |

Left: canals (`worldWaterPatchNode`, sea-level `global_ocean_patch`: water is not converted), building light wells with
no floor in the game's data either, small gaps; and skipped visible node types to add (water 3,048, foliage 2,103,
cables 218 in the slice). Spawns 0 of 16 stuck. Collision (slice-2): fall-through 8,573 m2 (Kabuki map-23, which the
owner walked: 3,148 m2 on about a third of the area), invisible solids 58,142 m2 (Kabuki 7,946), many of them roofs
and the elevated highway that the compare does not recognise as floor: the next check drops a player onto each listed
spot in game and reads where they land. Installed as Install 50's swap (map-2 + scene-3).

Speed: scene and collision build side by side in the recipe; with the bakes cached the slice's scene took 12 minutes
(24 cold) and the collision glb 3 (34 cold).

## 11. Water and the collision checks (2026-10-30)

Owner: "put 3 and 5 to the end, then start as you have them" (water, then confirming the collision lists).

**Water.** `scan` keeps `worldWaterPatchNode` (placements.json `water`: 870 patches in the slice box, 237 in its
radius; `global_ocean_patch.mesh` is a flat 10 m square, appearances ocean / swimming_pool / murky). The scene draws
one quad per patch (`WATER_LOOK`: dark, roughness 0.05, opaque; the wet-ground mirror reflects the street in it); the
build merges the patches into 25 boxes of water contents 4 m deep over a 1 m solid bed (`WATER_DEPTH`): 130 of the
237 patches had no collision under them at all. Slice-4 (scene) and slice-5 (map), not installed.

**The canal "hole" was the check.** The aerial camera at 10,000 u is past the far plane for anything under game z
-392 (`distanceCull` default 6,000 x 1.732); CP2077's sea level is z -450. The aerial now shoots from 40,000 u at 15
degrees, near orthographic, from a copy of the map with `distanceCull` set; holes on the slice: 526 m2 (the leaning
90-degree view had hidden some under tall buildings). The canals have render beds; their collision beds were missing.

**Collision compare, vectorised.** `levelcheck.py compare` gives look/kabuki_collide.py's results (Kabuki map-23 /
scene-24: every category's cells equal but 2 and 2 of 2.6 M) in 28 s instead of about 20 minutes, 2 minutes on the
slice. It leaves out water (and treats a solid top up to 4.5 m under water as its bed), and canopies: palm and tree
tops, umbrellas, and the grass and litter the converter leaves walk-through by name.

**Drop, every flagged surface.** A JKA player box (+-15, -24..+40) dropped on each fall-through surface through the
map's brushes (offline, 0.2 ms a drop on a 256 u brush grid), from 8 u over it or up to 14 u aside (the box still
covers the point); with no free start, the solid over it says why. The engine's own trace checks it: the game module
command `levelcheck_trace` (`codemp/game/g_levelcheck.c`, `trap->Trace` with MASK_PLAYERSOLID, a file of points in
and out), 5,000 of the drops (seed 0), tolerances fixed beforehand: a landing within 0.25 u, a start in solid. The
first engine run agreed on 4,813 of 5,000 and showed three places the offline model differed from `cm_trace.cpp`: the
sky shell's brushes (its floor stops every fall), touching counted as inside (CM_TestBoxInBrush), and the stop 0.125 u
off the hit plane (0.125 / nz straight down). With those: 4,979 of 5,000; the other 21 are exact-touch starts the model
calls inside and the engine not, where the engine's trace cannot move either (fraction 0). An earlier in-game version
(setviewpos, noclip, a real fall) needed allowances added after each mismatch (owner: "seem to be forcing it"); it is
gone.

| Slice-5 fall-through surfaces | m2 |
|---|---|
| falls (more than a step, under 27 m) | 439 |
| void (more than 27 m: the sky floor or the quest boxes 230 m down) | 133 |
| holds (within a step: a gap narrower than the player) | 1,530 |
| covered (a solid within 16 u over it: the floor under a prop box) | 4,672 |
| low (under 64 u of headroom) | 508 |
| blocked aside (no free start within 14 u) | 578 |

Invisible tops by height over the visible floor under them: bumps (18 u or less) 12,281 m2, lumps (18-64 u) 8,254,
walls and roofs 32,668, nothing visible under them 9,171.

**What the confirmed falls pointed at.** Mostly canopies (now not floors). One converter rule: a flat placement drawn
only in alpha-tested materials got no collision once its size passed `--col-box` (lawn tiles scaled to 6.4 m left 107
m2 with nothing under it); flat placements (0.5 m tall or less) now collide with their cutout chunks too (142 on the
slice). Left: balconies at exactly the 16 m collision height, boxes and window sills.

**Reference map.** The owner's Realistic Map 32K mod (zips/, not installed in the game) is a top-down render of the
whole city: 16 tiles of 8,192 px on 3,028 m quads (`3dmap_terrain.mesh`), about 2.7 px a metre, north up. Uncooked
locally with `ref_crop.py` (any CP2077 box) and `our_mosaic.py` (our aerial on the same box); our slice lines up with
it. The owner's "blocked roads" were the old perspective view: with the near-orthographic shots the roads match.

**Checks on the 64-bit client** (owner: "can you just use x64 to avoid waiting for other sessions"): staged once as
`levelcheck64.exe`, so other sessions' checks for a running `xenjk.x86*.exe` neither wait on it nor stop it.

## 12. Phase 1: tiles (2026-10-30)

Owner: "start phase 1: the tiled pipeline for Watson".

**Tiles.** `cp2gltf.py tiles` splits an area's placements into squares of `--tile-size` (256 m) on a grid with a
corner at the area's centre; a placement belongs to the tile its placed bounds' centre lies in (each mesh's bounds
centre cached in `mesh_bounds.json`), so an object across an edge is built once. What is in the area stays the area's
own rule (`in_area`), so the tiles together hold exactly what one build would. Each tile builds a scene part
(`scene --tile=<id>`: geometry, materials, instances; `tiles/<tag>/scene/<id>/`) and a collision part (`build
--tile=<id>`: collision triangles and prop boxes; `tiles/<tag>/build/<id>.npz`), each written whole then renamed.
`scene --merge` and `build --merge` join them (a mesh, material or geometry chunk several tiles hold is kept once) and
add what belongs to the whole area: lights, haze volumes, signals, the colour grade, water, the boundary, spawns. A
whole-area build is now the same code with one part. Images go through one cache per work folder (`scene_dds/`,
named by content), so a tile or a rebuild encodes only what is new.

**The gate passes** (`look/tiles-gate-20261030/`: `gate.sh`, `compare.py`, `compare_glb.py`). Kabuki whole and
through four 256 m tiles, compared by content: the scene's 65,088 instances (each by its frame and a hash of its
mesh's geometry and materials), 4,726 materials, 2,154 lights and light boxes, haze, grade, signals and 3,146 images
all the same, and the same vertex and index totals; the collision glb's 627,280 triangles and 1,768 entities the
same. Time (gate.log; corrected 2026-10-30, an earlier line here said tiles were faster): the scene took 17 minutes
whole and 23 through its four tiles plus 2 for the merge (each part repeats some whole-area work, below); the
collision 2.3 minutes whole and under a minute through tiles. The single build ran first and filled the image cache
the tiles then used.

**What the gate found in the converter** (both fixed; they changed one build too, not only tiles):

- **Materials were shared by `.mi` path, not by content.** Meshes override a shared `.mi`'s values (a window's room
  size, an ad screen's glow, a decal's image, `font_a` opaque on one mesh and glowing on another); whichever mesh
  came first set them for every mesh (13 materials, 1,023 Kabuki instances changed with the order). Now one material
  per distinct record (`material_key`): Kabuki 4,497 -> 4,726 materials. The collision build had the same key (a
  material's kind decides whether it collides).
- **Spawns depended on triangle order** (a seeded subsample of an unsorted list): the candidates are sorted first.

**Placement store.** A scan writes its placements by 128 m cell of their origin (`placements/<i>_<j>.jsonl`,
`index.json`); `placements.json` keeps the lights, entities, volumes and water. Readers stream it
(`iter_placements`); the fetch stages and `tiles` hold no full list (Watson's 1.6 M placements as one JSON would be
about 4 GB, some 13 GB loaded, on a 32 GB machine). Older scans' `placements.json` lists still read.

**Watson as an area.** `AREAS['watson']`: a 2,048 x 2,944 m box (the sector fetch) whose content is its 246 kept
128 m cells and nothing under -100 m (`tools/levelport/areas/watson_cells.json`, from the survey's
`density_grid_128.json`: keep_frac >= 0.5, near-empty cells out; 1,588,364 placements as the survey counted).

**The runner.** `tools/levelport/cp77_tiles.py <area> <tag>`: tiles, the parts on `--workers` processes (a part is
kept while a key over its placements, the converter's source and its arguments holds: an interrupted or repeated run
builds only what is missing or stale), the merges, gltf2map, the checks, and a per-tile summary (`summary.md`:
placements, meshes, materials, collision, holes and falls inside the tile).

**Machine notes.** 32 GB of RAM, 20 threads; D: is a spinning disk shared with other sessions' worlds: the tiles
step's first run (3,006 meshes' bounds) took 572 s on a busy D: against 168 s quiet.

**The slice through the runner** (`cp77_tiles.py watson_slice 6`, 2026-10-30; `cp77/watson_slice/tiles/6/`,
`summary.md`; build/`cp_watson_slice-6`, `cp_watson_slice_scene-6`, not installed). 12 tiles of 256 m (3,525 to
69,691 placements), 24 parts on two workers in 57 minutes, then the merges (scene 6 minutes, collision 1.5),
gltf2map 10 minutes and the checks 15: 89 minutes in all, on a machine shared with another session's 13 GB Blender
(three workers left 2 GB of RAM free and were stopped). Against the single builds (map-5, scene-4):

- Collision glb: the same 2,341,235 triangles and 8,070 entities (`compare_glb.py`). The BSP has 23 more brushes
  (959,222): gltf2map's brush building follows the triangles' order, which the merge changes (tile by tile); the
  collision compare moved by 7 of 3.09 M matched cells, the drop check's falls, voids and holds are the same m2.
  Engine traces agree 4,983 of 5,000.
- Aerial holes: the same 13 clusters, 525.8 m2. No seam shows: every hole is one map-5 has.
- Scene: the same 275,447 instances, 9,718 meshes, 46,325 parts, lights, light boxes, haze and grade. 533 materials
  more and 11,015 instances whose material record differs: scene-4 predates `material_key` (only in scene-4: none);
  300 k fewer vertices from the merge's geometry dedupe. The Kabuki gate is the like-for-like check.

**Fixed after the slice run** (found by profiling a part, `cProfile`: 126 s alone, 363 s beside the other worker):
every part read the whole folder's materials for the ad stand-in pool (50 s cold) and every collision part every
entity template for lights it never writes (28 s). The tiles stage now finds the pool once (`tiles/<tag>/ad_pool.json`,
part of each part's key) and a collision part skips the lights. Most of the rest was cold disk reads on the busy D:;
warm parts took 16-44 s. `cp77_tiles.py` now refuses new parts over an existing numbered build (a new tag) and, with
every part kept, skips the steps whose output exists, so a resumed run carries on. Parts run through the shared game
lock for the two client checks (`game_lock.py`).

**The patch checked** (tag 7, the slice again with it): the collision glb and the scene pack are byte for byte the
tag-6 ones. Parts took 15.5 minutes on two workers (tag 6: 57), the image cache warm. A fetch was writing into
folders linked to the slice's during the first try (below), so it was stopped and rerun.

**Watson's sectors, scan and fetch** (2026-10-30, `cp77/watson/`: `sectors-1.sh`, `fetch-1.sh` and their logs):

- `sectors --area watson`: 2,568 sectors in the box with a cell of margin (the survey's 1,699 are the kept cells'),
  7.5 minutes, 17.4 GB of JSON; `exterior_-17_21_0_0` read leniently, as before. Every exterior sector the survey
  kept for the area is in it (one level-1 sector outside every cell is not); the survey's exterior copy went to
  `.codex-trash/20261030T230325-levelport-survey-exteriors/` (11.2 GB), its interiors stay.
- `scan`: 2,089,672 placements of 11,510 meshes in the box (the cells keep about 1.59 M), 48,720 lights, 41,306
  entities, 16,390 water patches, 76,007 variant records off by default; 7 minutes. Placement store: 365 cells,
  660 MB; `placements.json` 45 MB.
- Seeded first (`seed_work.py watson watson_slice kabuki --apply`): 87,505 files, 14 GB linked, nothing copied.
- `fetch --ents`: 875 templates (10 missing), 83,281 entity mesh components; `fetch`: 10,233 meshes, 3,705 to
  uncook (the rest from the seed), 312 missing; `--lods` 322 missing; `--embedded` found 307 of the missing in
  the sectors; `--embedded-ml` 44 setups. 20 minutes for all five (a command-line overflow first: template paths
  now go to WolvenKit in batches, as meshes did).
- Work folder: 22.3 GB of its own (17.4 GB sector JSON, 2.3 GB new meshes and LODs, 0.7 GB store) and 14 GB shared.
  Bakes and scene images come with the parts.

**Seeded links get rewritten.** WolvenKit's uncook rewrites, in place, the textures and `.Material.json` a new mesh
shares with one already there: Watson's fetch wrote 16,163 of the slice's and Kabuki's files through the links,
16,162 at their old size (unverified byte for byte: no old copies). The odd one is a `meshes_lod` `.Material.json`
from the LOD fetch, a folder the converter reads no materials from. While a fetch writes into a seeded folder,
nothing may read the folders it links to: tag 7's first try read a half-written png and failed.

**For phase 3** (another session's link review, 2026-10-30, nothing measured): profile a slow slice view with an
Nsight Graphics GPU trace before building culling; a position-only vertex stream for depth-only passes if the trace
shows vertex fetch matters; content-adaptive variable rate shading (VK_KHR_fragment_shading_rate) is lossy, so the
owner's call and a look check first.

## 13. Disk: retention and Watson's budget (2026-10-30)

Owner: "we need to get through all of Watson without accumulating so much disk space debt that we run out".

**What was there.** A scan of `.codex-research/levelport` counted 441 GB, but 1,193 of the 1,198 packs under `look/`
are hard links to `build/` packs; by file identity the tree held about 106 GB (look 32 GB of its own, work folders
30 GB, builds 46 GB of which 30 GB linked from look runs). Kabuki alone had 23 maps and 24 scenes kept.

**Moved** to `.codex-trash/20261030T2002-levelport-superseded/` (29,830 files; 46.9 GB returns when the owner empties
it, none of it linked from anywhere else): superseded builds (Kabuki maps 1-21, scenes 1-22 and their variants,
slice maps 1 and 4, slice scenes 1-2, Dueling Keeps 2-4, the older NieR builds), the compile staging of installed
builds that are not the newest, the look runs' links to all of those, the survey's raw and sample extracts and phase
M's decoded extracts (`summary.json`, `m/summary_m.json`, `json_keep/`, `raw_keep/` and the scripts stay), the tiles
gate's packs, superseded collision glbs and the slice's pre-water scan. Kept: installed builds (Kabuki map-22 and
scene-23, slice map-2 and scene-3, the NieR pack GameData plays), the newest of each area (Kabuki map-23 and scene-24,
slice map-5 and scene-4, sound pack, NieR, Dueling Keeps 5), their glbs, the work folders and the reference map.
Undo: `python tools/research_shed.py --research .codex-research/levelport --restore 20261030T2002-levelport-superseded`.

**The rule, from now on** (`tools/levelport/retain.py`, the same choices as above, tested on a scratch tree):

1. After a build is checked or installed: `python tools/levelport/retain.py` (dry run), then `--apply`. It keeps
   every build GameData links (installed), each area's newest numbered build, anything made after it (in flight)
   and `--keep-build` names; staging only in the newest; moves the rest with every look-run link to it, and the
   work folders' glbs that no kept build was made from (`source.txt`, now written by gltf2map) except the newest.
2. Look runs hard-link packs, never copy them (unchanged).
3. One tiles tag per area, reused: parts are keyed, so a rebuild only redoes stale tiles.
4. Watson's fetch links what exists instead of making it again: meshes, LODs and bakes from the slice and Kabuki.
   Sector JSON cannot be linked (the slice's export of a sector differs from the survey's in the header's export
   time and path) and costs about 7 minutes to export, so Watson exports its own and the survey's exterior copy in
   `json_keep/` then goes to the trash (its interior folders stay for phase 4).
5. A survey or gate's bulk goes once its result is in a doc.
6. Before a district run: D: at least 30 GB free (one build with its staging and a margin); report a low drive
   instead of launching.

**Watson's budget** (estimates from Kabuki, the slice and section 2; not measured):

| Part | Size |
|---|---|
| Work folder: meshes, LODs, baked images (Kabuki 7.6 GB for 3,879 meshes, about 2 MB a mesh; Watson 9,970) | about 20 GB, 10-15 GB new once Kabuki's and the slice's are linked; **measured after the fetch: 2.3 GB new meshes and LODs, 14 GB linked** (bakes still to come) |
| Sector JSON | 12.3 GB, in place of the survey's exterior copy (shed once Watson's exists); **measured 17.4 GB** (the box's margin), the survey's 11.2 GB shed |
| Scan store, raw, entities | 2-3 GB; **measured 2.4 GB** |
| Scene image cache (`scene_dds/`, once) and one tiles tag's parts | about 4 GB and 2-3 GB |
| One build: scene pack (4.2 GB images, 1.2 GB geometry), map pack, collision glb | about 7 GB |
| Its compile staging (the slice's `.map` was 2.7x its BSP; the district's BSP about 1.2 GB) | about 4-5 GB |
| **Steady state** under the rule: work folder, caches, installed and newest builds, one staging | **about 40-45 GB** |

Kept the way Kabuki's builds were (every one), each Watson build would add 7 GB, 11-12 GB with its staging: twenty
builds would be 150-250 GB. D: has 124 GB free now and about 170 GB once the trash is emptied.

## 14. City-scale consult (2026-10-31)

Owner: "can you get fable and astra takes regarding phase 1 and 2 approaches to the whole district, collision,
q3map2?", then "analyze not just from the perspective of banging out watson, but eventually needing to do all of
night city". Brief and both answers, read-only: `.codex-research/levelport/look/district-consult-20261031/`
(`prompt.md`, `fable.md`: Claude Fable; `astra.md`: Codex `gpt-6-astra`, high). Scale they worked from: Night City's
exteriors are 15,823 sectors at levels 0-3 (9.3x Watson), about 12.7 x 11.4 km; one map spans about 3.5 km at
`MAX_WORLD_COORD` 64 k (q_shared.h:54).

**Where both agree** (and so the recommendation):

- **Watson is the first part of a chunked city, not the largest single map we can fit.** Per-tile scene and
  collision outputs plus a manifest are the product; today's district merge (one pack, one glb, one BSP) is a
  Watson-only stopgap, not to be hardened further (its walls, zip64 and a 12.7 GB merge process, are the merge's).
- **One coordinate space with larger fixed coordinates, no origin rebasing.** At the city's half extent (about
  238 k u) a float's step is 1/64 u against `SURFACE_CLIP_EPSILON` 0.125 u; the wire already sends full floats past
  +/-4,096 (msg.cpp:1118-1133). To be measured before anything depends on it.
- **A city-fixed tile grid** anchored to CP2077's sector grid, and area membership as "the tile is built", not a
  box or radius: today `stage_tiles` anchors tiles at the area's centre and `in_area` is a radius and cells.
- **Brush collision semantics stay** (startsolid, allsolid, touching counts as inside: what JKA movement is); a
  triangle-mesh or BVH backend only if a later measurement earns it.
- **CP2077's authored shapes become the first collision-source candidate**, converted to brushes (boxes, hulls;
  capsules and spheres approximated; triangle meshes as slabs), with render-derived slabs as the tracked fallback.
  Astra: 82 k shapes against 241 k brushes is not a compression ratio (a triangle-mesh shape can expand into many
  brushes); census and coverage first.
- **Patched q3map2 stays, as a per-chunk compiler.** No RBSP offset widening, no own writer for now.
- **Mirror CP2077's separation**: sector levels and their distance bands as residency inputs, occluders for long
  streets, collision tiers as contents, default variants only.

**Where they differ:** how collision chunks load at run time. Fable: the engine's sub-BSP path (`misc_bsp`,
`CM_LoadSubBSP`, cm_load.cpp:1024) with two fixes, a sub-BSP's model 0 keeping its tree (now a flat leaf of every
brush, cm_load.cpp:163-169) and per-tile unload, one entity per resident tile. Astra: not that path (it would make
static chunks gameplay entities; the trace takes surface flags from the main map's shader table, `cmg.shaders`,
cm_trace.cpp:614) but CM contexts of their own under a world-level spatial index. Both code facts checked here; the
two-chunk trace experiment below decides.

**Other points taken:** drop the 16 m collision ceiling for the city (Fable: highways, rooftops); key parts by stage
and by the fetched content they read, not the whole converter source (Astra: today's keys miss mesh and material
contents, and a step is kept when its output merely exists); export into private staging and publish immutable,
content-named files, hard-linking only those (the half-written png of section 12); the renderer still walks BSP nodes
and planes for the view leaf (tr_world.cpp:1356), so dropping its copy needs a compact topology; do not hide a tile's
levels 0-3 wholesale when its proxy shows (replacement groups first); do not keep sector JSON at city scale (about
17 GB a district: re-serialise on demand, about 7 minutes for Watson); bevel snapping is qualified apart from exact
plane dedupe.

**Measurements, a day or less each, in order** (the two lists merged): (1) coordinate reach, Kabuki translated to
about +200 k / +150 k u, compile, `drop --engine` and `compare`, one look at the far corner; (2) a census of
CP2077's collision on Kabuki: resolved references, placed against unique triangles, converted size, misses by
place and tier; (3) CP2077's shapes through the brush path on Kabuki against map-23: compare, drop, the owner's
report spots, swept boxes along streets (the trace command takes vertical drops only today); (4) two chunks against
one map: identical traces across the seam, through the sub-BSP path with its two fixes, time within 1.5x; (5)
q3map2 `_blocksize` 512 and 256 on map-23 (brushes a leaf, size, trace time). Owner decision pending on adopting
this direction for the rest of phases 1 and 2.

**Follow-up round** (2026-10-31, each answering the other with the code facts above; `followup-shared.md`,
`followup-fable-answer.md`, `followup-astra-answer.md`):

- **Runtime chunks, now agreed:** world-level dispatch. Chunk BSPs from the ordinary loader in clip maps of their
  own (one arena each), the existing brush collider, the world trace, position test and point contents also run
  over the resident chunks whose bounds the query touches, hits stay `ENTITYNUM_WORLD`, surface flags from the
  chunk's own shader table; leaves, clusters, areas and PVS stay with a small hub map. Fable withdrew tiles as
  entities (a hit would carry the tile's entity number, which game and cgame test in many places; configstrings
  read at cgame init; the renderer would load every tile as a world). Astra added: the tree fix alone breaks model
  contents (`CM_ModelContents_Actual` reads the synthetic leaf, cm_load.cpp:1094, checked); `misc_bsp` entities
  are permanent (g_misc.c:603, checked), so they take no snapshot slots but the cgame culls them at 5,500 u from
  their origin; handles depend on load order, so eviction needs stable chunk ids. Residency: a manifest of
  immutable chunk ids in the gamestate, shared by server, cgame and demos; absence never means empty space; both
  chunks resident first, eviction later.
- **The two-chunk experiment, merged:** freeze `kabuki-17.glb` and map-23's settings; make the slabs once; split
  whole brushes by bounds-centre x at map x = 0 (none cut, none duplicated); hub holds sky, spawns, ring; both chunks
  compiled open with the same `-bsp -meta`, no vis or light; the brush union compiled whole as the control. Extend
  `levelcheck_trace` (g_levelcheck.c) from vertical drops to arbitrary endpoints, bounds and masks: standing,
  crouched and point sweeps across the seam both ways, zero-length tests, start inside, steps, slopes, grazing edges,
  shader order deliberately different, chunk origins beyond 5,500 u. Fixed in advance: no classification,
  contents or flag mismatch; endpoint within 1/64 u; normals within 1e-5; time within 1.25-1.5x of the control.
  The control and the chunks must use the same kind of tree (the collision session, 2026-10-31): on these all-detail
  maps q3map2's tree is its 1,024 u block grid (up to 16,858 brushes a leaf, 2,742 tested by a trace on the slice),
  while gltf2map's tiled compile (`colmerge.py`, any world over 500 k brushes as q3map2 tiles merged into one BSP)
  tests about 20x fewer; mixed, the time tolerance would measure trees, not chunking. It runs both.
- **Coordinates:** no collision clamp at +/-64 k beyond patch windings (both); `G_DropToFloor`-style code uses
  `MIN_WORLD_COORD` (g_misc.c:411-417, Astra). Risks ranked: q3map2 far from the origin (its own bounds), sloped-plane
  distances (about 0.03 u rounding at 238 k u against the 0.125 u clip epsilon), the scene shader's world-space
  translation (1-3 px of jitter at 4K, fixed by subtracting the eye in the shader). Test: the corpus above at
  (0,0) and the four +/-238 k corners, compiled translated. The two proposed tolerance sets differ (Fable: trace
  endpoints 1/64 u; Astra: 1/16 u, pmove 1/8 u a frame, 0.5 px): to be fixed before the run, never after.
- **CP2077's collision is not proven smaller** (Astra, from `m/summary_m.json`): Watson's non-mesh shapes alone are
  about 882 k, and Kabuki's 15,155 resolved triangle-mesh placements hold 1.35 M triangles; 78% resolve by unique
  hash, not by placement. Convert per unique geometry (one convex brush where a mesh is convex, one box where it is
  box-like, coplanar slabs otherwise; no convex decomposition that changes contacts), budget placed brushes, and
  count before deciding.
- **The missing 22%** (Fable, from WolvenKit's GeometryCacheService source): an always-loaded section keyed after
  the per-sector runs (`AlwaysLoadedSectorDDB`), which `geocache_probe.py` never reads (it decodes only
  sectorEntries against bufferTableSectors, line 68, checked). Tests in minutes on the re-exported cache. Astra's
  other hypotheses (full keys, EP1 precedence, other cache resources) follow if a remainder stays.
- **The 16 m ceiling** goes for the city (both); render-derived collision above it should be limited per placement
  to where CP2077 has collision (Fable); measure capped against uncapped under the same bounds first (Astra).
- **Keys and publication** (Fable's design, Astra's critique): library entries keyed by their inputs (depot path,
  archive size and time, WolvenKit version, export settings), exported into staging and published by rename, never
  written again; part keys from the entry keys they read plus a per-stage rule version (fetch, scene, collision);
  every step keyed, none kept for merely existing; a sampled audit hash catches writes through links.
- **Renderer planes:** the view leaf walk needs nodes and their planes (tr_world.cpp:1356, tr_marks.cpp:142); load
  only the planes nodes use (a remap in `R_LoadPlanes`), not none.
- **Bevel snapping** is off by default (Astra's first answer was wrong there). Since then the collision session made
  `--slim-snap` default to 0: exact plane dedupe on, no snap; snapping was measured on its own (1 u changes 2.5% of
  Kabuki's drops, 1/8 u 0.6%) and stays off.

**Decided** (owner, 2026-10-31: "if you are finding agreement from them or your own wisdom to unite it then yes go
for it"): the chunked city. Watson is built as the first part of one city: a city-fixed tile grid, per-tile scene
and collision outputs and a manifest as the product, the district merge kept only as Watson's play export until
the runtime takes chunks. Where the two reviews still differed, settled here:

- **Tolerances for the coordinate and two-chunk tests** (fixed now, before any run): classifications, contents,
  surface flags and entity numbers identical; trace endpoints within 1/64 u after untranslating (the float step
  at the city's edge; Fable's, the stricter); fractions within 1e-6; normals within 1e-5; brush, side and plane
  counts equal across translated compiles; movement routes (Astra's jump, slide and strafe routes at 8 and 16 ms
  commands) within 1/8 u a frame with identical snapped velocities; projected vertices within 0.5 px at fixed
  1080p views; trace time within 1.25x of the control (Fable's, the stricter).
  Amended by the owner, 2026-10-31, after measurement 4 (not a refit: the owner's call on what the clause means):
  "Flag differences on exact ties and touching starts don't count against chunked collision, since any brush-order
  change produces them; endpoints, fractions and contents stay strict."
  For chunks compiled in their own frames (decided by this session, the owner delegating: "you can decide that
  one", 2026-10-31): the reference is the exact geometry, not the one map, which cannot hold the same float32 values
  and is the less exact far out. q3map2's double-precision planes per brush; a double-precision port of the brush
  trace that first agrees with the engine on the one map in at least 99.9% of traces; a corner passes when its count
  out of tolerance and its 99th-percentile endpoint error, both against the exact trace, are no worse than the one
  map's. The tolerances themselves unchanged; the gate as the collision session fixed it before any run
  (`docs/levelport-collision.md` section 15).
  And for a start buried in solid (decided by this session, the owner delegating: "its whatever you think is
  best"; Fable consulted): it counts as a touching start, its reported contents (those of whichever burying brush is
  tested first, order-dependent inside one map too) don't count; no engine change, the chunk dispatch visits chunks in
  the manifest's fixed order. Contents of a brush a trace enters stay strict.
- **Who does what.** The collision session (`docs/levelport-collision.md`) owns the runtime chunks (world-level
  dispatch in `codemp/qcommon/cm_*`), the trace command's sweeps, the five measurements of this section and the
  collision source. This session owns the pipeline: the city grid, per-stage keys and immutable publication, per-tile
  outputs and the manifest, the height cap's measurement. The renderer's per-tile scene loading is phase 3.

**Pipeline work done on the decision** (2026-10-31, this session):

- **City grid.** `cp2gltf.py tiles` anchors tiles at CP2077's world origin (tile `i_j` is the square
  `[256 i, 256 (i+1)) x [256 j, 256 (j+1))` m in every build; 256 m = 4 level-0 sector cells). Watson: **89 tiles**,
  1,618,378 placements in the area (the survey's 1.59 M), 4 to 90,197 a tile (median 11,779; the Kabuki market's
  `-5_5` the densest), `cp77/watson/tiles/1/`, 8 minutes.
- **Staged, never-rewritten publication** (`staging()`, `publish()`, `publish_copy()` in cp2gltf.py): every
  WolvenKit uncook batch and the embedded export write into a fresh staging folder on the cache's own volume; a file
  is moved into the cache only where none exists, by rename. Checked against WolvenKit on a scratch folder: two meshes
  fetched twice, the second time one texture already present as a hard link to another file: 87 files published,
  the linked one kept with the same identity, size and time.
- **Keys.** Part keys add the fetch identity (every game archive's size and time, the WolvenKit DLLs) and which of
  the tile's meshes are fetched (glb, materials, every-LOD glb): a mesh fetched later rebuilds the tiles placing it.
  The merges, gltf2map and the checks each have a key chained from their inputs' keys and their own tool source
  (`tiles/<tag>/steps.json`); a step runs when its key differs or its last output is missing; a numbered build is
  never overwritten by other inputs (a new tag). Tested: tag 7 resumed with every part and step kept, only the
  aerial rerun.
- **The aerial check could pass blind**: 0 of 14 shots (the client still loading the slice's images from a busy D:)
  read as 0 holes. `levelcheck.py aerial` now fails when a shot is missing, `drop` when an engine trace does not come
  back. Tag 7's aerial rerun: 14 of 14, the same 13 holes, 526 m2.
- **Watson's hot caches on the NVMe** (owner: "yes"): `meshes`, `meshes_lod`, `bake`, `scene_dds` (15.8 GB,
  counts and bytes checked) in `%LOCALAPPDATA%/xenjk-levelport/watson/`, junctions at the old paths (`to-ssd-1.ps1`,
  `.log`); the old names in `.codex-trash/20261030T235125-watson-caches-to-ssd/` (manifest). Watson's files are no
  longer links to the slice's or Kabuki's.

- **A tile-order dependence, found by the city grid** (tag 8, the slice on the new grid, against tag 7: everything
  equal but 5 instances of one mesh). A mesh with two stand-in ad faces (`ground_advertising_stand_b_screen_dst`)
  kept the variant on its second face only when another mesh in the same tile had converted that face's material
  first: `scene_mesh` asked `any(...)` over the mesh's materials and stopped at the first stand-in. Tags 6 and 7 had
  it too (variant 0), equal only because their tiles came in the same order. Fixed: a material asked for with a
  variant has its own record converted first, so whether it is a stand-in never depends on history. Guard: the scene
  merge now stops when two parts built one mesh key differently (LOD screens, index and vertex counts, material keys)
  instead of keeping the first. Check used: every mesh key's content compared across the parts holding it (tag 7:
  9,716 keys, 1 differing; tag 8 the same).
- **The slice on the city grid, verified** (tag 20: city grid, step keys, staged publication, the stand-in fix;
  66 minutes, `cp77/watson_slice/tiles/20/`). Against tag 7: the scene merge's new guard passed (no mesh key built
  differently by two tiles); the scene the same but 8 instances of the two stand-in ad meshes the fix changes; the
  collision glb the same 2,341,235 triangles and 8,070 entities; the BSP 3 brushes more (959,225: gltf2map follows
  triangle order, which the new tiles change), the collision compare within 3 of 3.09 M cells, the drop check's falls,
  voids and holds the same m2 (other bands within 0.7 m2), engine traces agree 4,987 of 5,000. The aerial check:
  14 clusters, 528 m2 against 13 and 526: one 1.3 m2 patch and two clusters 0.3-0.5 m2 larger, all where sky is seen
  through a balcony's glass railing (crops compared). The geometry there is the same in both builds, so this reads as
  blend order (translucent surfaces draw in instance order, and the tiles changed it): the check counts sky seen
  through glass. To do: classify holes from a depth or instance pass, not colour. Also: `levelcheck.py compare`
  took 16.6 GB on the slice; at district size it must run per tile.
- **Build names collided with the collision session's.** Its compiles used the names the tile runner gives its
  tags; tag 8's gltf2map refused the existing folder. Settled (the collision session, 2026-10-31): its builds are
  `build/cp_kabuki_col-24..30` and `cp_watson_slice_col-8..12`, the plain names are the tile runner's again
  (`cp_kabuki-23` the newest plain Kabuki build), and `retain.py` can run as far as its folders go. This session's
  slice tags continue from 20.

**Watson's parts, built** (2026-10-31, `cp77/watson/parts-1.sh`, `tiles/1/`, `summary.md`): all 178 parts of the 89
tiles in 88 minutes on two workers, none failed, caches on the NVMe. The district as parts: 1,601,949 scene
instances (phase M's inventory: about 1.64 M), at most 5,306 meshes, 6,257 materials and 4,040 images in one tile,
8,731 images in the cache; collision (render-derived, the 16 m cap) 11.78 M triangles and 490,039 prop boxes, the
densest tiles `-5_5` (Kabuki's market, 593,886) and `-5_10` (495,458). Parts on D: 8.6 GB (scene 7.7, collision
0.9); new bakes and images on C: 3.6 GB. Not merged (the chunked city); nothing compiled yet.

**The 16 m ceiling, measured** (`cp77/watson/cap-1.sh`, `.log`; render-derived collision parts, all else as the
runner's): six Watson tiles at `--col-height` 16 and 400 m:

| Tile | Placements | Collision triangles at 16 m | at 400 m | Ratio | Boxes 16 / 400 m |
|---|---|---|---|---|---|
| -5_5 (Kabuki market) | 90,197 | 593,886 | 1,010,938 | 1.7 | 30,072 / 42,205 |
| -6_4 | 61,035 | 194,843 | 853,769 | 4.4 | 13,102 / 24,947 |
| -8_5 | 42,128 | 190,144 | 674,058 | 3.5 | 9,825 / 19,816 |
| -8_9 | 24,091 | 176,561 | 367,894 | 2.1 | 7,122 / 11,475 |
| -9_10 | 14,065 | 187,699 | 270,972 | 1.4 | 5,744 / 8,420 |
| -4_9 (Northside) | 23,308 | 60,770 | 518,790 | 8.5 | 1,672 / 12,048 |
| **six tiles** | 254,824 | **1,403,903** | **3,696,421** | **2.6** | 67,537 / 118,911 |

Render-derived collision without the cap is 2.6x on these tiles, 8.5x where megabuildings stand. Scaled by
placements (an estimate, not measured): Watson about 9 M collision triangles capped and 23 M uncapped; at the slice's
0.41 brushes a triangle, about 3.6 M and 9.5 M brushes, against Kabuki's 241 k. Either way the district's collision is
chunked; uncapped render-derived collision is the case for CP2077's own shapes (census next, collision session).

## 15. Plan review after a day of results (2026-10-31)

Owner: "based on results so far, lets review plan and get fable+astra takes as well". Brief and answers, read-only:
`.codex-research/levelport/look/district-consult-20261031/` (`plan-review.md`, `plan-review-fable.md`,
`plan-review-astra.md`). Fable had the coordinate result (`docs/levelport-collision.md` section 8) folded in; Astra
answered before it.

**The coordinate result changes the city design.** One fixed coordinate space fails the fixed tolerances at the city's
corners (+/-232 k u): q3map2 builds different bevels far out, and the engine's side tests carry two float steps of
error there (a few dozen traces in 31,000 catch or miss an edge by over 8 u). Collision in each chunk's own frame
passes (offline): chunks are compiled about their own whole-unit origin and every query is translated into the
chunk's frame, the path `CM_TransformedBoxTrace` already takes for inline models; game state, snapshots and the
renderer keep the one fixed space. Fable withdrew "larger fixed coordinates" for collision. The manifest carries each
chunk's origin; q3map2 compiles every tile near the origin (the fourth q3map2 patch is not needed in production).

**Both agree:**

- Keep the chunked city. A merged Watson collision BSP is a play export the owner can walk, allowed (Astra:
  "permit, not require"; Fable: yes, built from the grid tiles), never the city's path: at 70-75% of the format with
  the 16 m cap, nothing after Watson fits it.
- Scene: no 4 GB `.xscn` and no zip64. One library (meshes, LOD chains, materials, image names) plus one instance file
  per tile, and `R_SceneLoad` reading the library then each tile file into the same buffers (a loop over a manifest,
  no residency yet); images split over a few pk3s. Checked: the `.xscn` is read whole into an `int` length
  (vk_scene.cpp:3200).
- The frame-rate gate is on the critical path now: the slice misses 120 fps while clipped at 277 m. First, profile
  the slice's worst view (r_gpuTimes / r_cpuTimes, an Nsight trace) on a quiet machine, before any renderer change.
  The likely first change, only if the profile supports it: reject tiles before the per-instance cull, which today
  runs over every instance for every view (vk_scene.cpp:3651, checked).
- Collision source: CP2077's shapes preferred, the census decides (unique geometry once, placed brushes budgeted,
  coverage and JKA contacts first). The 16 m cap goes for the city; Watson capped is a prototype limitation, recorded.
- `_blocksize` demoted: the merged tree already tests 150-171 brushes a trace against q3map2's thousands.
- The aerial check: classify from a material or coverage pass (glass over sky is not a hole); the blend-order reading
  of tag 20's difference is plausible, not shown (Fable: measure the check's noise floor first).
- Memory on the shared machine is under-weighted: budget peaks (compiles, loads, other sessions), and explain the
  64-bit client's extra 3 GB before a Watson load.

**Where they differ, and the order taken.** Astra puts trustworthy qualification first (keys, publication, per-tile
checks, the aerial classifier) before more expensive builds; Fable puts a playable export first, compiled from
city-grid tiles, because it is the only district-size renderer measurement and the only thing the owner can walk.
Both fit: the qualification fixes are small and mostly done, and the grid-tile compile needs them.

1. **Qualification fixes** (pipeline session). Done today after Astra's review: publication by hard link, so a racing
   publisher never overwrites (tested); a mesh's `.glb` published last, so an interrupted batch is fetched again;
   the map step's key over `colmerge.py` and `colbsp.py`, the checks' key over the 64-bit client and game module. Left:
   per-tile checks with neighbour overlap (`compare` took 16.6 GB whole on the slice); the aerial from a material
   pass after its noise floor; tile content independent of an area's selection (Astra: today `in_area` decides).
2. **Collision tiles by the city grid** (collision session). Each Watson build part compiled about its own origin
   (the densest, `-5_5`, about 243 k brushes, under the 500 k limit), merged by `colmerge.py` into Watson's export by
   translating each tile's planes; gated against the brush-count split on the slice. The per-tile BSPs are the
   runtime chunks; the two-chunk experiment uses them, with origins at the city's corners.
3. **Renderer, now** (phase 3 begins): the profile of the slice's worst view; the far-plane decision (Fable: CP2077's
   level-3 band, about 860 m, with the level 4-6 proxies beyond); the plane remap (`R_LoadPlanes` keeps only planes
   nodes use) before any Watson load; the library plus per-tile scene files. First steps done (2026-10-31,
   [levelport-render-district.md](levelport-render-district.md)): the worst view is GPU-bound geometry (the
   per-instance cull is 0.16 ms), occlusion culling recommended first; the plane remap gated (-88 MB Kabuki, -213 MB
   the slice), uncommitted; the 64-bit +3 GB is the NVIDIA driver committing the client's VRAM again, not RAM.
4. **CP2077 collision census and shapes on Kabuki** (collision session), with the always-loaded cache section.
5. **Watson's play export:** merged collision from grid tiles, per-tile scene; per-tile checks; the owner walks it.
6. **Runtime chunks** (world-level dispatch, chunk-local frames), the manifest in the gamestate, residency and
   eviction; then a neighbouring district as tiles only.

**Code gaps they found, not yet fixed:** an archive change does not invalidate cached exports (fetch skips an existing
glb; the city needs library entries keyed by the archives their resources live in); outputs without a recorded key are
still adopted when no part rebuilt; the merge guard compares structure (LOD screens, counts, material keys), not full
geometry. Astra also noted the collision record's trace command is no longer vertical-only.

**Not yet in any list** (Fable): 1,495 signals against 256 slots; the light grid q3map2 widens to 288 x 272 x 336 u at
district size (coarse character lighting until phase 3); disk (the export adds about 7 GB plus 5 GB of staging).

**Adopted** (owner, 2026-10-31: "Adopt the section 15 order"). Progress the same day:

- **Collision chunks from the pipeline:** `cp2gltf.py chunks --area <a> --tiles-tag <t>` writes each tile's collision
  as a glb in the tile's own frame (`tiles/<t>/chunks/<i>_<j>.glb`, `manifest.json`: each chunk's CP origin, its
  offset in the area's frame, counts): origin the tile's corner on the city grid at the floor (whole metres), the
  tile part's triangles and prop boxes, the water volumes whose surface's centre is in the tile; no ring, lid, sky or
  spawns. Checked on the slice (tag 20): the 12 chunks moved back by their offsets equal `watson_slice-20.glb`'s
  collision but its 492 ring and lid triangles; 66 of 2.34 M triangles differ by one 0.001 m rounding step (float32
  storage in the tile frame). Watson's 89 chunks: `cp77/watson/tiles/1/chunks/`, 678 MB, 11.78 M triangles, 490,039
  boxes, 1,601 water volumes, 5 minutes.
- **Chunk-local collision runs in the engine** (collision session, `docs/levelport-collision.md` section 9,
  uncommitted, a private x64 build): chunks load into clip maps of their own at whole-unit positions, the world trace
  and contents test also run over the chunks a query touches in the chunk's frame, hits stay world hits, surface flags
  from the chunk's table. Measurement 1 passes on it: Kabuki as a chunk at each city corner, 0 of 31,000 traces out of
  tolerance, endpoints within 0.0083 u, time 1.00-1.12x, contents identical at 15,000 points.
- **Sessions:** the collision session (its doc `docs/levelport-collision.md`) has the grid tiles next: Watson's
  chunks compiled about their origins, loaded in its runtime, and the merged play export from the same tiles (gated on
  the slice first), then the two-chunk experiment on grid tiles at the city's corners, then the census. The renderer
  session (phase 3; profile of the slice's worst view, the plane remap, the 64-bit client's extra 3 GB) times only in
  windows the collision session has cleared of heavy work.
- **The collision compare per tile** (`levelcheck.py compare --tiles T --tile ID`, `compare-tiles`, `stitch`;
  `cp77_tiles.py`'s compare step runs it on two workers, each tile started only into 6 GB free, a tile kept when
  its key holds). A tile owns the cells whose centres its square holds (`tile_owner`: past the tiles' bounding box a
  cell goes back to the box's edge, a square with no tile to the nearest tile), and loads every brush, instance and
  triangle that reaches them, its neighbours' included. Every rule of the compare reads one cell's column, so a
  tile's cells come out as a whole compare's; `stitch` puts the tiles on one grid as one compare's outputs, which
  `collide` and `drop` read unchanged. Two fixes on the way: the walk sampling runs in batches of 2 M triangles (the
  slice's densest tile peaked at 5.7 GB, now 2.0 GB; the whole slice 16.6 GB, now 3.9 GB), and the (cell, height)
  keys are exact (`lexkeys`, int64 by rank) instead of cell x 1e6 + z in float64, which rounds by 1/4,000 u at the
  slice's 3.3 M cells and coarser at a district's: the first per-tile run differed from the whole one in 50 of
  2.6 M cells, 48 of them a solid's top or bottom within the key's rounding of a rule's limit, many exactly on it.
  Gate (fixed before the run, `look/levelcheck-watson_slice-20/pertile-gate.py`), tag 20, exact keys both sides:
  the 12 tiles stitched against the whole compare, 0 of 2,617,661 shared cells differ; fall-through, invisible and
  visible samples the same multisets; solid intervals bit-identical in the tiles' cells; `collide.json`
  byte-identical. Cells only the whole compare holds: 647,593, corners of its rectangle past the tiles' reach, none
  with a finding. Tiles 5-52 s each, peaks 1.5-2.0 GB. Compares from before the exact keys (tag 20's in section 14
  among them) differ from today's in about 50 cells on the slice: 3,090,018 matched, 642,523 buried, 84,665 missing,
  384,127 invisible now, against 3,090,060, 642,468, 84,678, 384,124.
- **The drop check per tile** (`levelcheck.py drop --tiles T --tile ID`, `drop-tiles`; the runner's drop step). A
  tile drops its own flagged surfaces against only the brushes its columns reach (a drop's box goes 29 u aside:
  14 u of start offsets and the box's 15 u; straight down to the sky shell's floor), writing `drop_rows.npz`; the
  merge puts the tiles' rows on the stitched grid in a whole drop's order (cell, then height), then clusters, the
  engine's sample (seed 0 over the same starts, once, in one client) and the outputs as `drop` writes them, without
  loading the map. Each landing's side normal is now kept from the offline drop, so the engine's check no longer
  re-traces. Gate (fixed before the run), tag 20, both on the stitched compare, no engine traces: `drop.json` and
  `drop.png` the same, every row the same (84,665 surfaces dropped: start, rest, result, normal; 384,127 invisible
  tops banded). The engine path itself was not run (same starts, same order).
- **What a tile loads** (owner, 2026-10-31: "we might need to find processes where things can be done in smaller
  chunks if 8gb is hard to find free"). The first per-tile runs still read the whole map and scene before
  filtering: 1.5 GB even for a tile with 151 brushes, a floor that grows with the district. Now the map's collision
  lumps (shaders, planes, brushes, sides; `BspLumps`) and the scene's tables, then only the index and vertex ranges of
  the LOD 0 parts a tile's instances draw (`Ranges`), are streamed out of the pk3s, never whole. Same outputs, bit
  for bit (compare: the smallest and densest tiles; drop: tile -6_5 and the gate above). Peaks: compare 0.43 GB
  (smallest tile) to 1.72 GB (`-5_5`, its own 16.5 M walkable triangles and 368 k brushes), drop 0.44 GB, the whole
  slice's drop 0.7 GB. Streaming costs about 12 s a tile on the dense one (the scene inflated three times). Start
  gates from these peaks: `compare-tiles` 3 GB free, `drop-tiles` 2 GB. On a shared machine free memory swings
  between under 1 and 3 GB (other sessions' Blender batches and clients): a floor that kills a 0.44 GB job on a dip
  only loses its work, so the runs here wait to start and are not killed. Still whole: the merged `.xscn` itself
  (Watson's would pass the renderer's 4 GB `int`; per-tile scene files are the city path) and a tile's own content.
- **The checks' client and the game lock** (owner, 2026-10-31: "are we not on x64 and everyone else on 32"). The
  sessions' lock (`.codex-research/anime-batch-20261030/scripts/game_lock.py`) counts other clients as `xenjk.x86.exe`
  only; the checks' 64-bit client runs as `levelcheck64.exe`, which no lock or session counts. A slot for it guarded
  nobody and only made the checks queue, and held the renderer session's exclusive timing back (it waits for every
  slot free, with no priority over new takers). Now `cp77_tiles.py` takes no slot for the checks; the client
  (`levelcheck.client_popen`) waits only while one process holds every slot (a timing run, `timing_run_holds`).
  Left open: a check started just before a timing run takes the lock can overlap its first minutes; the renderer
  session checks for `levelcheck64.exe` before timing.
- **Blank aerial shots.** Tag 7's last two shots were black frames (the run took 799 s under disk contention, against
  154 s), counted as shot. Their pixels count as seen and not sky, so where they overlap a good shot they dilute its
  holes, and a black shot over the level would read as ground without holes. Both lay at the level's edge, covered
  by other shots inside the checked radius: tag 7's holes stand (its 2.1 m2 difference from tag 20 lies in shots 1, 4
  and 5). `aerial` now sets a frame whose brightest pixel is under 8 aside as `blank_*.png` and fails, as for a
  missing shot.
- **Collision by grid tiles, the collision session's gates** (its report, 2026-10-31; detail in
  `docs/levelport-collision.md`). The slice: its 12 grid chunks compiled about their origins (`gltf2map --col-chunk`,
  `build/cp_watson_slice_colchunks-21`; `-5_7` holds no collision), the export `base.glb` plus the chunks merged at
  their offsets (`gltf2map --grid-chunks`, `build/cp_watson_slice_col-13`, 958,211 brushes). Against slice-20, both
  under the same frozen `levelcheck` (HEAD's, float keys): spawns 0 of 16 on both; compare 134 of 3.27 M cells differ
  (matched 3,090,039 against 3,090,060, buried +26, missing -5, invisible tops equal); slice-20's 5,000 engine
  traces replayed on the export, 4,958 identical, landings 1,502 of 1,505 identical, the other 3 within 0.033 u. The
  runtime's 11 chunks (hub away) trace 2.6x faster than slice-20. Passes at the earlier gates' level, a little noisier
  than build to build: q3map2 snaps plane distances in absolute coordinates, so a chunk's own frame snaps a little
  differently. Measurement 4 (Kabuki split at x = 0) **fails** section 14's no-flag-mismatch clause, through tie
  order only: the same brushes in two chunks against one map, 0 endpoint and 0 fraction differences, 139 flags from
  ties and 19 touching starts flipped (swapping q3map2's tree for `colmerge`'s inside one map gives 876 and 76);
  chunks compiled separately add plane-choice noise, 18 endpoints over 1/64 u, at most 0.115 u. Chunks trace in 0.24x
  the control's time; the four corner placements give identical results. The clause stands as fixed: whether a
  tie's flags count is the owner's decision, not a tolerance to refit after the run. The owner decided (section 14's
  tolerances, amended): tie flags and touching-start flips don't count; endpoints, fractions and contents stay
  strict. So the dispatch (the same brushes in two chunks) passes if none of its 139 tie differences is a contents
  difference (the split asked of the collision session); chunks compiled alone still fail on endpoints (18 over
  1/64 u) and fractions (1,269 over 1e-6): q3map2's plane choices per chunk, the open item. Owner: "q3map2 can be
  whatever we need it to be": the collision session patches it so a chunk compiled alone matches the whole compile
  (first suspects: distance snapping in the chunk's frame, FindFloatPlane's first-come merging). Watson's 89 chunks compiling
  (62 done at the report); its export and an 89-chunk runtime load next.
- **Watson's boundary ring follows its box.** The ring and lid were a circle of the area's radius (1,794 m, which only
  reaches the box's corners): +-66,900 u, past q3map2's +-65,536 world and the engine's +-64 k; q3map2 dropped them
  and the sky shell and stopped ("Backwards tree volume", `build/cp_watson_col-1`; the collision session). An area of
  a box (`AREAS` 'half' a pair: Watson) now gets four playerclip walls 1 m outside its box and the lid 3 m over it
  (`box_ring_boxes`): +-38,475 x +-55,275 u, the lid +-55,312 u at most; circle areas (Kabuki, the slice) keep theirs.
  Watson's `tiles/1/chunks/base.glb` rebuilt (`build --merge --base-only`, 29 min, 7.0 GB peak: it reads all 89
  parts): 5 collision nodes against 41, the same 39,179 lights and spawns; the circle's kept as
  `base-circle-20261031.glb`. Watson's 89 chunks are compiled (`build/cp_watson_colchunks-1`: 82 with collision,
  7,601,933 brushes, the densest 394,439); the export `cp_watson_col-2` and an 82-chunk runtime load next.
- **The collision session's gates, scored** (its report, 2026-10-31; `docs/levelport-collision.md` sections 10-13).
  The slice through grid tiles **passes** against slice-20: `cp_watson_slice_col-13` 0 of 16 spawns stuck, the
  compare within a few cells, drop classes within 3 m2 (falls 438 / 439, void 131 / 133, covered 4,675 / 4,672);
  the engine side is slice-20's 5,000 traces replayed, 4,958 identical (the frozen `levelcheck`'s engine stage
  stalled on both maps loading images, 0 of 5,000 back: the stage linked the 1.6 GB scene pack beside the map, which
  the traces never read; it now links the map only). Measurement 4 under the owner's amendment: the dispatch has 0
  ends and 0 fractions out of tolerance; 2 contents differences remain, both on one start buried in solid (all-solid:
  the contents come from whichever brush is tested first), for the owner to rule on as a touching start or not;
  chunks compiled alone still fail strictly (1,269-2,295 fractions, ends up to 0.13 u), the q3map2 patch's work.
  Watson's 82 collision chunks load through `cm_loadchunk`; the 15,744-trace corpus runs in 1.8 s at a 4.4 GB peak;
  the load takes 674 s (about 1 s a compressed MB; inflating is 1 s of the largest chunk's 26 s): to profile before
  streaming. The census (measurement 2): the always-loaded cache section closes the missing 22%, every Kabuki shape
  resolves (25,985 of 41,140 mesh placements from it); "Complex Environment Collision" is query-only (shots and
  sight, never the player) and holds 1.83 M of the 2.82 M placed triangles; the player-blocking rest converts to
  about 0.49-0.92 M brushes for the market box against map-23's 240,836, so CP2077's shapes are not smaller, and
  measurement 3 needs a real conversion. The export `cp_watson_col-2` compiling on the box-ring base.
- **Measurement 4 re-scored under the amendment** (`m4/m4_amended.json`, `docs/levelport-collision.md` section 11).
  The dispatch's 158 differences: 156 flag-only ties or touching-start flips, 2 contents differences, both one start
  buried in solid (3,080 3,416 83, swept standing and crouched): all-solid at fraction 0 in both maps, its contents
  those of whichever burying brush the engine tests first. Under contents-strict the dispatch fails by those 2
  (colmerge's tree inside one map gives 4 against q3map2's). The seam and corner arms: 4 contents, 1 all-solid and
  1 entity difference each, and still their ends (18, 22) and fractions (1,269, 2,295). No compile fixes a buried
  start's contents: an order-free rule (the OR of every burying brush's contents) is an engine change, proposed by
  the collision session, not built; the owner's call. The q3map2 plane patch: the collision session's next work, its
  gate fixed beforehand (Kabuki's measurement-4 corpus, chunks compiled alone against the whole, both by the patched
  q3map2: ends 1/64 u, fractions 1e-6, contents strict). Its engine trace runs adopt `timing_run_holds` and take no
  slot during an exclusive run.
- **The buried start, decided.** Fable (asked for the owner): an OR of every burying brush's contents, at the chunk
  dispatch only, so vanilla maps are untouched; little game code reads a buried start's contents (pmove's water test
  on the ground trace, bg_pmove.c; NPC nav's botclip test), surfaceFlags are never set on a buried start, entity
  numbers are the world's. The start here lies 3,080 u from the split, so its burying brushes are likely in one chunk,
  where a dispatch-level OR changes nothing. Decided (owner: "its whatever you think is best"): a buried start counts
  as a touching start, no engine change, the dispatch's chunk order fixed by the manifest. Measurement 4's dispatch
  passes: 0 ends, 0 fractions, 156 ties and touching starts and 2 buried-start contents excused. Chunks compiled alone
  still fail: the q3map2 plane patch.
- **Watson's play export built, the q3map2 plane patch** (the collision session, 2026-10-31; its doc sections 12 and
  14, gate `look/collision-20261030/m4x/GATE.md` fixed before the run). `cp_watson_col-2` on the box-ring base:
  7,601,944 brushes, merge 271 s, light 556 s, 0 of 16 spawns stuck; loads in 40 s at a 7.1 GB peak. Against the
  82-chunk run (which loads no ring, lid or sky: 5,007 traces end there): 4 ends over 1/64 u (at most 0.045 u), 928
  fractions over 1e-6, 1 contents difference: planes moved into the area frame, rounded. At this size the chunk
  runtime is much slower: 674 s to load and 1,755 ms to trace the corpus against the export's 40 s and 128 ms
  (Kabuki's two chunks cost nothing): `CM_LoadChunk` and `CM_TraceChunks` to profile. The plane patch
  (`tools/levelport/q3map2/exact_planes.patch`, build xenjk5, options off by default): distance snapping was not a
  cause; first-come plane reuse was (`-exactplanes`: only an exactly equal plane is reused, -0 stored as +0), and two
  more: edge bevels from float windings in the compile's frame (now the brush's own double windings, anchored at a
  whole-unit point) and 4-decimal plane points (`-pointgrid 16384`: at most 3e-5 u). Kabuki's measurement 4 with it:
  the seam arm (halves compiled alone in the map's frame) 240,035 of 240,035 brushes bit-identical to the whole
  compile (110,420 differed before), 0 ends, 0 fractions, 4 contents differences all on buried starts: passes. The
  corner arm (halves about their own origins): normals identical but in 4 brushes, distances within 0.005 u (float32
  in another frame), in the engine 862 fractions over 1e-6 (median 0.0007 u along the trace), 5 ends over 1/64 u (the
  worst a grazing contact deciding a ledge 495 u up). Open for the owner after the move: the reference for chunks in
  their own frames (proposed: chunks and the one map each against an exact double-precision trace of the same brushes,
  chunks no worse; the gate written before the run). Not adopted yet: gltf2map passes no patch options; Kabuki's gates
  against map-27, then Watson's chunks rebuilt, after the move.
- **Occlusion culling, approved** (renderer session; its report `docs/levelport-render-district.md` section 1a). A
  dev counter (`r_sceneOcclusion`, worktree only) tested the eye's instances against the frame's far-depth pyramid,
  4K, exclusive lock: hidden share 99.3-99.9% on the slice's three phase M views, 91-98% on Kabuki's; GPU frame with
  the hidden ones left out, slice 17.0 -> 9.5, 14.9 -> 7.9, 12.1 -> 5.3 ms, Kabuki 10.1 -> 7.0, 9.6 -> 5.9, 9.9 ->
  6.7 ms (the prepass to 0.1-0.3 ms, the lit pass -3.7 ms; the sun cascades unchanged, 5.3 ms on the slice); images
  the same within the shots' noise; the test 0.1-0.3 ms. An upper bound: those slice views each face a wall a few
  metres off, and the camera was still. Owner: "Yes, build it". In order: street views that really look down the
  roads (and their measured share), then culling with last frame's pyramid reprojected and a disocclusion pass behind
  a cvar, gated (fixed before each run) on GPU time on all views, Kabuki's lap, and shots against culling off, still
  and along the lap; a report before any install. The plane remap is committed (4d212187, not pushed).
- **Kabuki's collision with `--exact-planes`** (collision session, `gate-kabuki/GATE-col31.md`, fixed before the
  build): `cp_kabuki_col-31` against map-27 passes all four items (spawns 0 of 16; compare matched equal, buried and
  missing within 3 cells; drop classes within 1 m2; 4,998 of map-27's 5,000 engine drops within 1/64 u). The
  own-frame gate failed as written on a confound in its design: the corners' endpoints are world float32 near x
  232 k (a 1/64 u step) against the one map's near the origin; from each trace's own fraction in double the corners
  score 0.000598 u against 0.000612 u and 24 fractions over 1e-5 against 26. Restated (this session, the owner
  delegating): both scored the same location-neutral way, tolerances unchanged, the raw numbers kept beside; the 1e-6
  count under it: one map 699, each corner 717 (all fractions over 1e-6; ends over 1/64 u 23 and 21; contents 2
  each); 99th-percentile end 0.000612 against 0.000598 u: fails on the count, recorded in `m4x/GATE-ownframe.md`
  with the world-space numbers beside. The cause found in q3map2: a face whose normal is not snapped stores
  dist = float(n_double . p0) beside the float32 normal, off by (n_float - n_double) . p0, which grows with the
  coordinate and so differs between frames. Fix under `-exactplanes` (build xenjk7): every face distance as
  n_float . centroid in double, box bevels from the double bounds; then measurement 4 and this gate again, and
  Kabuki's gate against map-27, before xenjk7 replaces xenjk5. Watson's chunks rebuilding on xenjk5 meanwhile
  (`cp_watson_colchunks-2`, export `cp_watson_col-3`: the baseline for the load profile and traces).
- **Chunked collision passes in every frame** (collision session, `m4x2/ownframe_score.json`; its doc section 15).
  The q3map2 fix (build xenjk7, `exact_planes.patch`): under `-exactplanes` a face's distance is the stored float
  normal through its points' centre in double, box bevels at the double winding bounds. The own-frame gate as
  restated, tolerances unchanged, all arms compiled again: passes at all four corners, out of tolerance one map 664
  against each corner 605, end p99 0.000596 against 0.000536 u (xenjk5: 699 / 717, 0.000612 / 0.000598); the seam
  arm still 240,035 brushes bit-identical; the float32 port still bit-identical to the engine on all 52,500.
  Kabuki `cp_kabuki_col-32` (xenjk7) against map-27, criteria fixed beforehand (`GATE-col32.md`): passes (spawns 0
  of 16, compare within 4 cells, drop classes within 1 m2, 4,998 of 5,000 replayed drops within 1/64 u).
  `gltf2map --exact-planes` now selects xenjk7. Watson rebuilt on it from 12:14 (`cp_watson_colchunks-3`, export
  `cp_watson_col-4`, both traced, the chunk load re-timed on the NVMe); the xenjk5 run stopped at 30 of 89 chunks
  (`cp_watson_colchunks-2`, a partial: junk once colchunks-3 is checked).
- **The scene as tiles** (`cp2gltf.py scene --merge --tiled`; format `docs/levelport-scene.md`, "Tiled scenes,
  version 1", reviewed by the renderer session). Every file an ordinary `.xscn` v3 with some sections filled: per tile a
  geometry shard (the meshes it is first to place) and an instance file (its instances, and the lights whose origin
  is in its square), one material table, a text manifest `maps/<map>.xman` (tile bounds with height, counts); indices
  global (identical geometry is stored once for the scene, so a shard may point into an earlier one); images over
  packs of at most 1.5 GB; every file under 1 GB, no zip64. Written one part at a time (`SceneMerger`, the merge's
  joins split out; `merge_scene_parts` and `scene_finish` now use it and `xscn_blob`). Gate (fixed before the run,
  `look/tiled-scene-20261031/check_tiled.py`): the slice tiled (`build/cp_watson_slice_scene-21`, 2.5 min, 1.06 GB
  peak) laid end to end against scene-20: vertices (12.8 M), indices, meshes (9,718), LODs, parts, materials (10,202)
  and instances (275,447) byte for byte, the 8,879 lights with their boxes the same multiset, `.xfog`, `.xlut`, `.xdet`
  byte for byte, the same 5,494 images: passes. Watson's built: `build/cp_watson_scene-1` (8.6 min): 90 tiles (89
  and the water), 1,610,399 instances of 21,282 meshes, 30.5 M vertices, 28.4 M triangles over all LODs, 18,546
  materials, 8,617 images in 2 packs, 41,398 lights (under the clusters' 65,535); every file under 1 GB.
- **Occlusion gates re-fixed** (decided by this session, the owner away: "you keep things running and progressing.
  dont wait on user input"): the still-image gate as the held-still comparison (the shots were 1,300 frames apart:
  sabers, signals, reflections moved), the as-written result kept beside; the split made cheaper and gate 3 re-run with
  its +0.15 ms limit unchanged.
- **The aerial check: its noise floor, and holes from what is drawn** (`look/aerial-noise-20261031/`). Three repeats
  of slice tag 20's aerial and the run on file: every pair 0 pixels apart in the sky test, the same 14 clusters, 527.9
  m2: no noise floor; a difference between builds is the builds'. Tag 7 against 20 (2.1 m2, three patches) was then
  read with a material-index aerial (`aerial --debug 8`, the same cameras; `holes-kind`): at all three a solid,
  opaque surface is drawn (a street-marking or a wire-fence material; the view's 12 bits leave two candidates) whose
  flat albedo passes the colour test for the magenta sky. Not glass drawn in another order (section 14's reading
  was wrong): colour false positives. Across the slice the colour test finds 528.8 m2 in 14 clusters, nothing drawn
  at all 504 m2 in 10 (sky pixels: 120,352 open, 4,958 under a solid surface, 1,103 mixed, 75 under glass). The check
  now shoots the index view and calls a pixel a hole only where it is exactly the sky's clear colour (`aerial --debug
  8`, the default; `--debug 1` the old colour test): the slice's 10 real holes kept, the 4 false ones gone.
- **The level port moved to C:** (owner, 2026-10-31; the record as kept in the handoff until the evening)

  **Done 11:29; corrected 12:5x: it lives at `<levelport root>`.** `.codex-research/levelport` is a junction to
  `<levelport root>\levelport`, Watson's four cache junctions to `<levelport root>\watson\*` (every tool and path
  unchanged). The first target, `%LOCALAPPDATA%\xenjk-levelport`, was not real: the packaged Claude app's
  processes (and everything they start) have AppData writes redirected into the package's own
  `AppData\Local\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local`, invisible to any process outside the app and deleted
  with an app reset (found by the collision session; checked from a WMI-started cmd: "File Not Found" for the path and
  the D: junction). Watson's hot caches had been there since 00:23 too. Fixed by one rename on C: out of the package
  cache (`levelport-move/out_of_appdata.py`, owner: "Yes, move it") and the junctions re-pointed; checked from outside
  the app (the tree, the D: junction, the caches, a pack all read) and inside (a slice tile's compare byte-identical).
  The same redirect holds every other agent folder under `%LOCALAPPDATA%` (the x64 builds, `xenjk-play*`, `xenjk-p3`). `relocate.py copy`: 103.3 GB in 5,467 s, every file hashed from the source and again
  from the copy, hard links kept (a pack and its look-run names share one file), the 4 inner junctions recreated; small
  files ran at about 40 a second off the spinning disk. `switch` checked every name and size first. Smoke: a slice
  tile's compare through the junction byte-identical to the one before the move. The verified D: copy is in
  `.codex-trash/20261031T112937-levelport-moved` for the owner to delete (about 100 GB back on D:). The steps as run:

  Owner: "once it ends let me restart claude and then you move and then we continue work after that is all done". The
  whole of `.codex-research/levelport` (work folders, `build/`, `look/`; Watson, the slice, Kabuki, QL, NieR) moves to
  the NVMe, a junction left at the old path; other xenjk conversions stay on D:. Tool:
  `.codex-research/levelport-move/relocate.py` (scan / copy / switch; tested on a tree with inside and outside hard
  links and a junction, 2026-10-31). Its scan of the real tree (`.codex-research/levelport-move/scan.log`, 2026-10-31
  09:40): 117.7 GB unique in 185,072 files (cp77 63.4, look 31.5, build 21.9, the rest under 1 GB; 2.4 GB also linked
  outside, so not freed), 4 junctions (Watson's caches, already on C:). Before the copy: `retain.py --apply` (its dry
  run: 9.0 GB of superseded builds, `levelport-move/retain-dry.log`) with the collision session's `--keep-build`
  names, and its own choice for `look/collision-20261030` (14.2 GB). Done 09:35 (owner: "whatever you know is junk,
  drop in trash and ill delete"): `retain.py --apply` keeping the collision session's builds whole moved nothing (the
  9.0 GB were all its superseded builds); `levelport-move/trash_junk.py` moved 4.25 GB of superseded check runs and
  tests to `.codex-trash/20261031T093449-levelport-junk` (restorable with `research_shed.py --research
  .codex-research/levelport --restore`; the owner has emptied it). Then with the collision session's list (keep
  `cp_kabuki_col-27`, `cp_watson_slice_col-13`, `cp_watson_slice_colchunks-21`, `cp_watson_colchunks-1`,
  `cp_watson_col-2`, and `cp_kabuki-23` whose `stage/` its m4 scripts read): `retain.py --apply` moved 7.27 GB to
  `.codex-trash/20261031T093822-levelport-retain`. Its look runs' pk3 and bsp copies are all links to kept builds
  (moving them would free nothing): left. Its fuller list then: `cp_kabuki_col-25`, `-26`,
  `cp_watson_slice_colchunks-20` (1.44 GB, `.codex-trash/20261031T094438-levelport-retain`), and from
  `look/collision-20261030` the census exports and the map and pack files of district/, far/, gate-kabuki/,
  gate-slice/, q3m_k23* and its run homes, every result, log and script kept (319 items, 15.06 GB of names, at least
  3.98 GB freed, `.codex-trash/20261031T094506-levelport-collision-bulk`). About 90-100 GB to move.

  1. Wait for the collision session's `cp_watson_col-2` export to finish (it messages), and no writer under
     `.codex-research/levelport` (no `cp2gltf`, `gltf2map`, `q3map2`, `levelcheck`, `colmerge` processes); both
     sub-sessions were asked to start nothing there until told.
  2. C: free at least the scan's unique GB plus 20 GB.
  3. `python -I .codex-research/levelport-move/relocate.py copy .codex-research\levelport
     %LOCALAPPDATA%\xenjk-levelport\levelport` in the background (resumable: rerun after any stop;
     it stops if a file changes under it).
  4. `relocate.py switch` with the same paths: the old tree becomes `levelport.moved-<stamp>`, a junction takes its
     name. Smoke: read a slice build and a Watson tile part through the old path.
  5. Tell both sub-sessions the move is done. Ask the owner before removing `levelport.moved-<stamp>` (that frees D:).
     Hard links to installed packs in GameData stay on D: (the scan counts them).
- **Occlusion culling, round 2: passes, committed** (renderer session, 2873dbc8 on renderer/pbr-materials: renderer
  files and `tools/scene_spirv.py`; not pushed, not installed; `r_sceneOcclusion` 1 by default; committing it was
  this session's call under the owner's delegation). The cost cut: phase 2 over phase 1's hidden list only (an
  indirect dispatch), its lists given only the commands it touched, the pyramid five levels a dispatch: 0.36-0.54 ms
  down to 0.24 ms a frame. Gates on one binary, timing in an exclusive window: held-still images 18 of 18; the moving
  lap 90 of 90; GPU time no worse on all 18 views (the slice's street views -0.5 to -5.1 ms, its wall-facing views -8
  to -9.4; Kabuki -0.05 to -3.6; `street_i` -0.14 ms, +0.18 before); Kabuki's 4K lap 104.4 -> 108.8 fps (+4.2%).
  Report `docs/levelport-render-district.md` section 1b. Next there: `R_SceneLoad` over the `.xman` manifest.
- **Watson on xenjk7, and runtime chunks from the NVMe** (collision session, its doc section 12). `cp_watson_colchunks-3`:
  89 chunks (82 with collision), no failures; export `cp_watson_col-4`: 7,601,944 brushes, 0 of 16 spawns stuck. The
  82 chunks load in 27 s (43 s start to loaded) against 674 s from D:, trace the corpus in 130 ms (the export 151 ms)
  at a 4.5 GB peak (the export 7.4 GB): the slowness was the saturated spinning disk, not the chunk code. Export
  against chunks: 3 ends over 1/64 u (at most 0.041 u), 941 fractions over 1e-6, 1 contents (the area frame's float
  rounding). A junction trap: a script that finds the repository by `Path(__file__).resolve()` under the levelport tree
  now lands on `<levelport root>` (its frozen levelcheck did; `tools/levelport` is outside the junction).
- **`retain.py` after the move to C:** (1) it found installed builds by hard link alone, and the builds and GameData's
  installed copies no longer share any: a dry run would have trashed Kabuki map-22 and scene-23 and the slice's map-2
  and scene-3 (all installed). It now also counts a build file installed when a GameData file has its name and size
  and the same sampled hash (first and last MB). (2) Its trash was the repository's `.codex-trash` on D:, which a
  rename from C: cannot reach (moved 0, skipped 190; nothing half moved): the level port's trash is now
  `<levelport root>\.codex-trash`, on its own volume, for the owner to empty like the other.
- **Watson's first district-wide collision compare** (`look/levelcheck-watson-col2/`): the xenjk5 export
  `cp_watson_col-2` against the tiled scene, per tile (89, two workers), stitched: 25.65 M cells, 64.8 M walk samples;
  visible surfaces 28.02 M matched, 5.46 M buried, 1.13 M missing (3.2%); 2.81 M invisible collision tops; clustered,
  17,468 fall-through clusters over 110,325 m2 and 17,619 invisible-solid clusters over 412,620 m2 (candidates: the
  drop check decides which drop a player). Its drop stopped once `col-4` landed; the same check on `col-4` (xenjk7) is
  running (`look/levelcheck-watson-col4/`), compare and drop.
- **Fetch caches keyed by the archives** (Astra's gap: a game patch left cached exports stale, since fetch skips what
  a cache has). `meshes/`, `meshes_lod/`, `ents/` and `apps/` record the game archives' and WolvenKit's identity
  (`cp2gltf.fetch_identity`, the one `cp77_tiles.py`'s part keys already use) in `.fetch_identity.json`; a cache made
  before adopts today's once, and a different identity stops the fetch with the changed archives named (move the cache
  aside and fetch again). Refetching only what a changed archive holds needs an archive-to-file index: the city's.
- **Watson loads in the renderer** (renderer session, `docs/levelport-render-district.md` section 1c; the `.xman`
  loader joins the manifest's files into one scene in memory, the rest of `R_SceneLoad` unchanged). Gates fixed
  beforehand: the slice's scene-21 joins to scene-20's exact counts and bytes (27 files, 521,448,704 bytes); `cp_watson`
  with col-2 in the 64-bit client: 181 files, 1.31 GB joined, 1,610,399 instances, 41,398 lights, 8,618 images (none
  missing), no errors, street views render; load 55-72 s, peak commit 12.1 GB (8.4 of it the VRAM mirror), working set
  5.1-6.0 GB, VRAM 8.3 GB. The same-image gate failed as written on 3 of 10 slice views: each view drops the player 3
  m and the landing follows each load's frame timing; with the camera locked by a demo's fixed frames scene-21 and
  scene-20 are identical on 6 of 6 (0.000% of pixels). Re-fixed as the camera-locked comparison (this session, under
  the owner's delegation), the as-written result kept beside; the loader to be committed (renderer files). Found on the
  way: the installed Install 50 pack's single `.xscn` hid a staged tiled scene (the manifest is now read first). Next
  there: the visible buffers sized by one LOD (384 -> about 166 MB on Watson), then Watson's frame times at 4K on col-4.
- **Measurement 3, CP2077's own player collision for the Kabuki market** (collision session; its doc has the full
  record): against map-27 (render-derived, 240,029 brushes, 0 spawns stuck, 28,117 visible surfaces with no
  collision), the shapes alone (col-35) 313,709 brushes, a spawn with no floor, 119,138 surfaces uncovered; shapes and
  terrain (col-36) 628,717 brushes (2.6x, past the gate's 2x) and a spawn in solid. A decision for render-derived
  collision was taken on these, then **suspended**: the collision session found its converter put the slab behind
  each triangle's counter-clockwise front, and CP2077's PhysX meshes are double-sided with no consistent winding (43%
  of walkable triangles, and every heightfield triangle as decoded, face down), so slabs sat 16-50 cm over the floors
  (2,007 of 3,000 sampled uncovered floors): the coverage and hole numbers partly measured the defect. Fixed (floors
  and slopes face up); `cp_kabuki_col-37` on the same criteria decides. The brush counts stand. Final (collision
  session's section 17, criteria never changed; four converter defects fixed on the way, each failed build kept):
  `col-38`, shapes plus the 33 terrain heightfields, floors up, every shape reaching the measured square: a stuck spawn
  (a 0.86 m CP2077 ramp under it), holes 1,008 m2 (map-27 122), 780,035 brushes (3.25x); matched 512,227 against
  506,521 and missing 1.3x, invisible collision tops 4.4x. **Decided** on these (this session, under the owner's
  delegation): the city's collision stays render-derived; CP2077's shapes a candidate only as a targeted, separately
  gated supplement. The runtime-chunks design (collision doc section 16: manifest in the hub's pk3, its name and
  checksum in the gamestate; the hub keeps every entity and model; residency by players and bots, 3x3 tiles with
  hysteresis, asynchronous loads, a memory cap, stand-ins for long traces) reviewed: projectiles and thrown entities
  to drive residency, a per-chunk occupancy proxy for long sight traces, the client-server residency invariant stated
  and measured, four far-apart players in the memory measurement. Built after its open measurements.
- **Watson col-4 (xenjk7) per-tile compare**: within a few hundred cells of col-2's (matched 28,017,663, buried
  5,462,914, missing 1,129,833; clusters 110,338 m2 fall-through, 412,635 m2 invisible solid): the compiler fix changes
  precision at the tiles' edges, not coverage. Its drop (per tile, 45 min, 1,129,833 surfaces dropped, 2,813,407
  invisible tops banded): fall-through m2 falls 9,977, void 3,233, holds 41,064, covered 47,986, low 8,088, blocked
  10,658; invisible solid m2 bump 108,112, lump 65,243, wall or roof 173,135, nothing visible under 101,330. About
  13,200 m2 where a player drops through (the slice's 572 m2 at a sixth of the instances): relatively worse than the
  slice, and the largest clusters repeat (a dozen of 700-930 m2 in the north, y 39-47 k u, about 110 m2 of falls
  each): a few mesh classes, being named (`fall_meshes_tiled.py`).
- **What Watson's falls stand on** (`look/levelcheck-watson-col4/fall_meshes_tiled.py`: 4,000 of the 141,920 falls and
  voids sampled, each named by the drawn LOD 0 triangle under it; all 4,000 named). Two causes. (1) Large placements
  drawn only in alpha-tested materials get no collision (`collision_tris` keeps cutouts only for a flat piece): the
  landfill terrain prefabs (`landfill_sparse_lp`, `_deg45_lp`, `_coarse_lp`, about 7,000 m2 of the sample area) and
  `ramon_container` (2,009). (2) `cp77_tiles.py` builds with `--col-tris 0`, and `collision_level(ml, 0)` then takes
  every large mesh's coarsest LOD (phase M's setting, when one 2 GB map had to hold the district): of the opaque falls,
  the coarsest LOD has no surface at the spot for 5,199 m2 and one further than a step for 7,467, within a step for
  3,443 (`fall_coarse_check.py`). Meat factory tanks (5,335 m2), backwalls, Little China building pieces, cooling
  towers. The worst tiles: -5_13 (2,609 m2), -4_13 (1,989), -8_5 (1,602), -4_12 (1,363). Measuring the fixes there,
  two arms through one pipeline (`look/colfix-20261031/`: `--col-tris 4000` and the new `--col-cutout-large` against
  today's arguments; the gate in `run.sh` and `score.py`, fixed before the run).
- **The merged export is at its limit; a playtest hub instead** (collision session). `cp_watson_col-4`'s BSP is
  2,076,065,164 bytes, 97% of the format's 2 GiB (int32 lump offsets; brush sides 1.06 GiB, planes 0.69 GiB, planes
  already deduplicated): any collision fix adding more than about 3% brushes overflows a merged Watson. A listen server
  shares the engine's collision maps between server and client prediction, chunks included, so a hub map (today's
  `base.glb`: ring, lid, spawns, lights) and an exec cfg loading every chunk at its manifest origin give all of Watson
  without a merged export (82 chunks: 27 s, 4.5 GB peak, traces as fast). Decided (this session, the owner away): the
  collision session builds and gates that hub now (spawns, col-4's drop corpus replayed within 1/64 u under the
  amended tie rules, load time and memory), chunks swappable, nothing installed; the merged export no longer caps the
  collision fix. The 64-bit client carries the chunk code (uncommitted) and is what full Watson's renderer needs.
- **The collision fix, measured and adopted** (`look/colfix-20261031/`, score `score.json`; three arms through one
  pipeline on Watson's four worst tiles, ownership as in the whole tiling, the gate fixed before the run; the
  baseline arm reproduced tag 1's parts byte for byte):

  | Arm | Falls + void (m2) | Map brushes | Densest test tile |
  |---|---|---|---|
  | xa: `--col-tris 0` (the coarsest LOD; the runner until now) | 10,269 | 687,871 | |
  | xb: `--col-tris 4000 --col-cutout-large` (the finest LOD under 4,000 triangles) | 2,972 | 2,615,004 (3.8x) | about 805 k |
  | xc: `--col-tris 4000 --col-walk-lod --col-cutout-large` | 2,998 | 1,323,159 (1.9x) | about 389 k |

  Per tile (falls + void, xa / xc): -8_5 2,679 / 1,052, -5_13 3,234 / 888, -4_12 2,146 / 836, -4_13 2,210 / 222.
  `--col-walk-lod` (`walk_level`): per mesh once, the coarsest LOD of at most `--col-tris` whose up-facing triangles
  lie within 0.3 m of 95% of points sampled on LOD 0's up-facing triangles; walls do not make falls, so a mesh whose
  coarse LOD keeps its floors keeps its coarse brushes. Adopted (this session, the owner away): `cp77_tiles.py`'s
  build arguments are xc's; Watson's collision rebuilt as tiles tag 2 (the scene parts and placements hard-linked from
  tag 1: the scene is unchanged), then its chunks for the collision session's compile into the playtest hub. What is
  left in those tiles (about 3,000 m2) to be named once tag 2's per-tile check runs.
- **levelcheck reads collision chunks** (`look/chunkmaps-20261031/`: `run.sh`, `score.txt`, `f32.py`). A merged
  export can't hold tag 2's collision: col-4 is 97% of the BSP's 2 GiB, and xc has 1.9x its brushes. So Watson's play
  collision is now chunks only: the hub's map plus a chunk pack, loaded with cm_loadchunk (this session's decision, the
  owner away). The checks follow. A `map` argument may now be several comma-separated pk3s, each with one BSP or more;
  their collision lumps are laid end to end with every index moved. A pack's `cm_loadchunk <bsp> x y z` cfg lines
  place its own-frame chunks: each plane distance plus n.T, in float64. A tile's check loads only the BSPs whose
  placed world-model bounds reach its window (compare: the tile's reach; drop: reach + 32 u). Results:
  - A single map is unchanged: slice tile -6_6's compare and -6_5's drop rows are identical to the runs before the
    change.
  - Watson -4_12 on hub + colchunks-3's pack, against col-4 (the same chunks merged): the same 127,141 brushes. 36 of
    360,000 owned cells differ (3 visible surfaces buried rather than missing, 3 more invisible tops), and 2 of 43,582
    drop rows (`falls` and `holds` become `blocked`).
  - With the placed planes rounded to float32, as col-4's merge stores them (`f32.py`), 0 cells differ. So the 36 cells
    are exactly that rounding, which the own-frame rule doesn't count (collision doc section 12).
  - The check costs the same per tile: compare in 25 s, drop in 64 s.
  Tag 2's per-tile compare and drop: `compare-tiles <hub>.pk3,<chunks>.pk3 <scene> ...`, then `drop-tiles` on the same
  map list.
- **Hard links don't cross volumes, so installing a pack takes a copy.** GameData's `Play_*.bat` on D: link packs with
  `mklink /H` into `.codex-research/levelport/build`, which is on C: since the move (collision session, 2026-10-31).
  Packs linked before the move are unaffected.
- **Server memory and chunk loads, measured on tag 1** (collision session; `docs/levelport-collision.md` section 16,
  plan fixed before the runs). On a 64-bit dedicated server, a resident chunk costs about 1.3x its BSP:
  - the hub alone: 0.29 GB;
  - the densest 3x3 of chunks: 0.82 GB;
  - the four densest 3x3s that don't overlap (four players, worst case): 2.08 GB;
  - all of Watson: 3.05 GB.
  Loads are CPU-bound: each chunk takes under 0.5 s on NVMe, on D:, or on D: under load. The old 674 s was D:
  saturated by random reads. A 3x3 loaded on tile entry leaves a 9,600 u tile of margin, about 10 s at 1,000 u/s,
  about 20x the worst load. Tag 2 has about twice the triangles, so it should need residency on the 1-4 GB server
  target: about 6 GB for all of Watson, about 4 GB for the worst four players (both estimates). That makes runtime
  chunks (next step 3) a requirement for tag 2 on a real server, not only for the city; it is next after tag 2's
  checks.
- **Collision residency, first build** (collision session; `docs/levelport-collision.md` section 19, gate fixed
  first; engine change uncommitted). The server loads chunks within 9,600 u of any client and frees those beyond
  19,200 u. Chunk memory is on its own zone tag, freed per chunk. That also fixes a listen-server hazard: the client's
  hunk clear could free placed chunks.
  - A dedicated-server walk through the densest tile, 127 k u, passes: 0 invariant violations, and 5,120 of 5,120
    traces identical to an all-loaded reference.
  - Peak memory 1.06 GB, 0.56 GB at the end, against 3.05 GB with everything loaded.
  - Loads are synchronous: the worst stalls a frame 884 ms, which can't ship.
  Decided (this session): an asynchronous loader next, after tag 2's measured resident cost. Its gate, fixed first:
  the worst frame within one 20 Hz frame, traces identical, a stated rule for a client whose tile isn't ready, and
  the walk held to 2 cores (the server target). Projectiles and long traces as drivers follow as their own step.
- **Watson's playtest kit launches, and the haze cost has a cause.**
  - **Kit smoke** (collision session, approved by the renderer session): `build/cp_watson_hub_col-1` with the
    renderer's committed lod rd-vulkan (a32d66cf) loaded the whole tiled scene. That is 1.61 M instances, 28.4 M
    triangles, 0 images missing. All 82 chunks were in place 13.7 s after the start, and cgame init finished at 49 s,
    peak 8.85 GB private. **Not passed:** the process ended about 1 s after cgame init (65 s), before its scripted
    frames and its quit. No crash event, no exit code captured, cause unknown. It will be rerun with the exit code,
    stderr and a dump.
  - **Async chunk loader** (collision session): built. Its gate was fixed first
    (`look/collision-20261030/res/GATE-residency-async.md`) and the runs are in progress. The rule for a client whose
    chunk isn't in yet: held, its commands skipped.
  - **Haze** (renderer session, `docs/levelport-render-district.md` section 1f): almost all of the haze pass's Watson
    cost (about 4 ms) comes from the shader's 256-volume limit per cell group. Watson has 606 local haze volumes, so
    every cell tests all of them. Decided (this session): raise the limit to 1,024 and gate it on bit-identical held
    frames, with the slice no slower. Then cull lights and volumes to the view before the cluster build, which also
    lifts that ceiling for the city. The cluster build is 1.2-1.4 ms because every screen tile tests all 41,398 lights.
- **Haze volume fix, measured; held on gate 1** (renderer session, `docs/levelport-render-district.md`). Raising the
  shader's per-group volume limit from 256 to 1,024:
  - Watson's haze pass goes from 4.2-4.5 ms to 0.32-0.61 ms, about 4.1 ms faster a frame (ns_street_a 7.3 ms, 137 fps).
  - The moon's in-haze cost falls to 0.02-0.06 ms, so it was volume-bound.
  - The slice is unchanged (-0.03 to +0.08 ms). Registers stay at 55; shared memory grows from 32 to 128 B.
  Gate 1 (bit-identical frames) failed, because the committed build doesn't reproduce itself: two runs differ by
  0-322 pixels on 22 of 32 camera-locked frames, at fixed small spots. Decided (this session): find that flicker in
  the baseline first and rerun gate 1 unchanged, rather than amend a gate after a failed run. Only if the flicker
  proves inherent, state a noise-floor method from separate baseline runs before fresh runs. The fix stays
  uncommitted until then.
- **Spawn preload** (collision session, coded, not built): the async gate's first-frame failure was a client
  entering play before its tile was resident. Map load now pins the 13 chunks around the hub's 16 spawns, and clients
  wait at connect or respawn until their region is in. The gate is unchanged; the build waits for the renderer's
  timing window.
- **Residency tests corrected** (collision session). The walk's drops started at z 1,200, above the hub's sky shell
  (top 984.5), so 2,492 of 2,560 drops landed on the shell and touched no chunk. The first gate's "5,120 identical"
  therefore tested only the 2,560 horizontal sweeps (section 19 corrected; run 1 kept). Runs 2 and 3 drop from z 600,
  under the lid. The spawn preload is built (uncommitted, not installed) and the async gate is rerunning unchanged.
  Tag 2's memory, walk and hub spawn checks follow, then the kit smoke alone; this session's tag 2 per-tile checks
  stay paused until those end.
- **The hub's gate tested no drops** (collision session). Section 18's corpus started all 5,248 drops at z 1,200,
  above the sky shell: 4,931 ended on its top and 317 fell outside the ring. What reached chunks were the 10,496
  sweeps at z 600 and the 32 spawn checks. Section 12 (chunks with no hub) stands. The re-take,
  `hub/GATE-hub-retake.md`, was fixed before its runs: section 18's rules unchanged, with the drops at z 600. It adds
  a hub-alone run, so the traces that reach a chunk can be counted, and is queued behind tag 2's chain.
- **Async collision residency passes its gate, unchanged** (collision session; `docs/levelport-collision.md`
  section 20):
  - The worst server frame is 12 ms, within the hub-alone run's 1 ms plus the 50 ms allowed.
  - 5,120 of 5,120 traces match the all-loaded run, and 2,557 of them really reach chunks.
  - 0 invariant violations.
  - On a slow disk, the held driver's 16 drops after release match the all-loaded run.
  Loading the spawn region at map load takes 2.3 s. On the real disk the worst background load was 223 ms read plus
  50 ms parse, so a teleport into an unloaded tile freezes its player for up to about a quarter second.
  Decided (this session):
  - Spawn chunks are not pinned by default (`cm_chunkPinSpawns 0`). Pinning holds 0.77 GB for the whole session on
    tag 1, too much for the 1-4 GB server target; a respawn into an evicted region waits as part of the respawn, and
    that wait gets measured.
  - Bots are held by the same rule. They aren't yet, since they think through the game rather than SV_ClientThink.
  - The chunk manifest is read by the map load itself.
  - Then projectiles and long traces, gated.
- **The renderer's flicker has a cause:** draw order within a culled command follows atomic slots (plus occlusion
  phase 2's appends), so the same frame can draw same-depth geometry in a different order. Serializing the cull cut
  flickering frames from 22 of 32 to 6 of 25. Direction given (this session): order-stable compaction (a prefix sum
  over visibility marks), then gate 1 rerun unchanged.
- **Tag 2's resident cost, measured** (collision session, 64-bit dedicated server, private memory; tag 1 in
  brackets):

  | Chunks loaded | Tag 2 | Tag 1 |
  |---|---|---|
  | Densest 3x3 (9 chunks) | 1.23 GB | 0.82 GB |
  | Four far corners' 3x3s (18 chunks) | 1.17 GB | 0.69 GB |
  | Four players' worst case (34 chunks) | 3.67 GB | 2.08 GB |
  | All of Watson (82 chunks) | 5.63 GB | 3.05 GB |

  The largest chunk has 659,422 brushes (compiled as tiles); its worst in-place load is 654 ms. The async walk on
  tag 2 passes the unchanged gate: worst frame 12 ms, 0 invariant violations, 5,120 of 5,120 traces. Its worker read
  in at most 277 ms and parsed in at most 80 ms; the walk peaked at 28 resident chunks (2.13 GB of chunk blocks) and
  ended at 2.02 GB private. Pinning the 13 spawn chunks would hold about 1.1 GB on tag 2 (an estimate; measured: 21
  chunks resident after the walk, 1.86 GB of chunk blocks, 2.02 GB private), which supports decision 1 (no pinning);
  the bot runs with pinning off give the measured difference. Four players at Watson's densest spots come close to a 4 GB server's limit, so chunk memory is the
  next cost to cut for the 1-4 GB target, after the checks: brush count per chunk, or a tighter eviction distance.
- **Tag 2's playtest hub and the hub re-take pass** (collision session; `docs/levelport-collision.md` sections 18
  and 21).
  - Hub col-2 (from tag 2's base.glb): 0 of 16 spawns in solid, and 16 of 16 drops find a floor within 64 u. All 82
    chunks are placed with no errors, loading in 31.6 s with a 7.25 GB peak.
  - The re-take, with drops under the lid: 5,194 traces reach a chunk (3,135 drops, 2,043 sweeps, 16 spawn checks),
    each identical between the hub and the far hub.
  - Against col-4: 5 ends over 1/64 u (worst 0.047 u), 941 fractions over 1e-6, 0 contents, 0 all-solid
    differences. That is the export's float32 rounding.
- **Residency decisions 1-3, built; a loader ordering bug found and fixed** (collision session, runs 4-5; the scored
  run 6 follows the renderer's timing window).
  - **The bug:** the worker thread was started by a frame's first queued job, so it could take a farther chunk
    before the driver's own was queued. That doubled the slow-disk hold, from 10 s to 20 s. Now a frame queues all
    its jobs under one lock, then starts or wakes the worker.
  - **Walk, before the fix:** worst frame 38 ms (limit 51). That is up from 12 ms, because the main thread now frees
    the evicted, unpinned spawn chunks; moving the frees to the worker is a follow-up. 0 invariant violations, 5,120
    of 5,120 traces.
  - **Bots:** a held bot stays at its teleport point. With the flag off, it falls to the shell's bottom, so the test
    sees a fall.
  - **Respawn waits** with spawn chunks evicted: 10 holds over 8 kills, median 749 ms, worst 899 ms.
  - **Gate change before scoring:** bot check 3 was amended before any scored run, because Runner_v2 runs off once
    released (`res/GATE-residency-bots.md`).
- **Renderer flicker fixed; the haze volume fix passes gate 1 unchanged** (renderer session, report section 1g; round
  2 in an exclusive window, 21:47-22:10).
  - **The fix:** each LOD range's instances are sorted by instance index after the cull (`scene_sort.comp`, one
    workgroup per range). Phase 2's touched commands are sorted too, then appended in order by a scan.
  - **Results:** the order build reproduces itself on 32 of 32 frames (0 px), and the volume fix's gate 1 passes, 0 px
    on 32 of 32. Cost against today's atomic path: the slice -0.18 to +0.11 ms, Watson +0.02 to +0.11 ms (the sort
    itself 0.06 ms).
  - Decided (this session): commit the order fix, then the volume fix (renderer files and report; no push, no
    install).
  - **Open:** frame 12 (Watson's first shot after load) showed a two-state 18 px variant in 2 of 10 runs outside a
    window, beside other clients. It gets rerun in an exclusive window after the view cull.
- **C: cleanup** (owner, 2026-10-31: C: down to 62 GB). Since 14:00, 55 GB of real data had been written, each file
  counted once: compile staging, chunk builds, colfix arms, renderer look runs. The look runs' 40 GB of names are
  mostly hard links to a few packs. Moved to `<levelport root>\.codex-trash` for the owner to empty (28.4 GB, two
  stamps, each with a `MANIFEST.tsv` for `research_shed.py --restore`):
  - `retain.py --apply`, with every live Watson build named (`.codex-research/KEEP.txt`): 20.9 GB. That covers
    colchunks-1, Kabuki col-27 and col-31 to col-37, the colfix arms' staging, and the look-run links to those packs.
  - The colfix arms' remaining bulk, their tile parts and tiles tag 1's superseded collision parts: 8.1 GB. Scores and
    logs stay.
  Kept: col-2, because the renderer's `occ/watson` runs link it.
- **Residency run 7** (collision session; the worker now frees evicted chunks). It passes the async gate unchanged:
  - worst server frame 0 ms, against 38 and 77 ms before the move;
  - 5,120 of 5,120 traces;
  - 0 invariant violations;
  - on the slow disk, held about 10 s until its own chunk swapped in, then 16 of 16 drops match.
  Memory returns to 0.56 GB after the walk, and the spawn preload takes 1.4 s, against 2.3 s before.
  Bot check 3 failed as amended. The held bot behaved right: held until its chunk swapped in, then it hopped on the
  street (z 420-619) and never fell. The criterion's drop from a fixed z 600 found an upper walkway (z 594) instead.
  Ruled (this session): the method was wrong, not the tolerance. My first replacement, a trace down from the bot's own
  origin, could never fail; the collision session caught it. Adopted instead, its path check: in the all-loaded
  reference, a point trace between consecutive samples must hit nothing (fraction 1 within 1e-6). It must also pass on
  a self-test run with everything loaded, so chords that cut step edges would show (if any does, sample more finely;
  the tolerance doesn't change), and the control must fail it. The first landing is recorded with no pass mark. All of
  it goes into the gate before a fresh scored run 8, and run 7's check 3 stays recorded as failed under the old
  criterion.
- **Respawn waits and the kit crash, explained** (collision session, run 7).
  - **Respawns** with pinning off, the bot 30,000 u away between deaths: after JA's own respawn delay (1.1-1.2 s),
    4 of 8 respawns waited 0 ms and the rest 0.8-1.1 s (9 holds, median 799 ms, worst 1,099 ms).
  - **The kit's earlier silent exit** was a crash: 0xC0000005 right after CL_InitCGame, reproduced with exit code
    3221225477 and a dump. Its exe predates the hunk fix (section 19), so most likely the client's Hunk_ClearToMark
    freed the hunk-placed chunks under the traces. System commit was 51 of 70 GB, so not exhaustion. The same kit
    with the current exe ran its frames and quit with exit code 0.
  - **`build/cp_watson_hub_col-1`'s kit must not be played** (a DO_NOT_PLAY.txt beside its .bat).
  Decided (this session): a fresh kit `cp_watson_hub_col-3` (the current exe, tag 2's hub plus the manifest pack, the
  committed renderer), gated with a functional smoke. Not installed.
- **Traces past the loaded region (residency step 4), ruled before coding** (collision session's proposal,
  `look/collision-20261030/res/GATE-residency-traces.md`). Every chunk within 9,600 u of a player is loaded, so traces
  near players answer as on the full map. For the rest (a projectile flying past every player's region, long sight
  lines, entities far from players), `cm_chunkUnloadedSolid 1`: a chunk that isn't swapped in counts as one solid box
  over its placed bounds. A rocket bursts at the boundary, unseen; an entity in an unloaded chunk is stuck instead of
  falling. Approved (this session), on two conditions:
  - Loading is decided by distance to a chunk's placed bounds, and a per-frame invariant checks that no non-resident
    box lies within the load distance of any driver. Placed bounds can reach past a tile, and a box there would be a
    false wall beside a player.
  - The known limit is measured and reported: players more than about 19,200 u (512 m) apart may not trace to each
    other, which covers bot sight, hitscan and projectiles between them.
  Not doing: projectiles as drivers (a rocket would load the city it crosses), or an occupancy proxy.
- **The residency bot gate passes in full** (collision session, run 8 on run 7's build;
  `res/GATE-residency-bots.md`).
  - **Path check:** the self-test with every chunk placed has 0 hits in 159 segments, so sampling every 2 frames is
    fine enough. The held bot has 0 hits in 159. The control, with holding off, has 3 hits, the first through the
    street floor at z 405, so it fails as it must.
  - **The hold:** the bot alone loaded -5_6, and it was held at its teleport point for 104 of 104 samples until the
    swap.
  - **First landing (recorded):** z 429.1, 7.5 u above the drop under the release point, on another step 245 u away
    (TeleportPlayer's push).
  With run 7's async gate and spawn waits, residency's decisions 1-3 are done. Next: hub_col-3's smoke, then step 4
  (unloaded chunks as solid) with its gate.
- **A playable Watson kit: `build/cp_watson_hub_col-3`** (collision session; `hub/GATE-kit-col3.md`; not installed).
  It holds the current chunk-build exe, with residency, the hunk fix and the hold flag; the renderer's committed
  e6837414 (order and haze fixes); tag 2's hub and manifest pack; and scene-1. The owner plays it from its
  `Play_Watson_Chunks.bat`.
  - Its smoke passes: frames ran, it quit with exit 0, and there is no dump. The manifest was read at map load (82
    chunks). The 13 spawn chunks loaded in 2.1 s (1.27 GB on tag 2), and chunks were ready 7.2 s after launch.
  - Peak private memory 9.94 GB; system commit at most 53 of 70 GB.
  - A real client's spawn wait read -1 ms (a flag OR-ed into the start time), so run 7's spawn waits are biased by
    at most 1 ms. Fixed in the step 4 build.
- **Step 4 (unloaded chunks as solid) is built.** Trials: the fault mode counts false walls on 400 of 400 frames (an
  unloaded box 8,880 u from the driver), and the rocket test samples the rocket to its burst. The owner said
  benchmarks take minutes, not an hour (memory `benches-take-minutes`), so run 9 is cut to what step 4 can change.
- **The owner played Watson (2026-11-01, kit hub_col-3): "quite impressive".** 16 /screenpos reports, triaged in
  `look/review-20261101/playtest_triage.md`. Most are converter bugs:
  - **Invisible trees with solid trunks:** SpeedTree's image is `DiffuseMap`, and the stand-in's alpha 90 made the
    cutout drop every tree (611 materials, about 25 k placements). Fixed in `cp77mat.py`.
  - **Floating traffic lights:** embedded entity templates (street lamps, poles) were skipped: 2,697 entities, 79
    templates, 6,145 mesh components on Watson. Fixed in `cp2gltf.py`: `write_embedded_templates`, and Entities with
    `embedded=True` for fetch --ents's meshes only, since the scan already reads their lights.
  - **Magenta and blue billboards:** the stand-in ad pool took test patterns. Fixed: `real_ad`, a deny list plus 128
    px and luminance std 12.
  - **Missing shutters and "floating ads":** skinned components weren't converted. Fixed: `entSkinnedMeshComponent`
    and `entPhysicalSkinnedMeshComponent` at bind pose; NumberPlateScreenMesh UI meshes skipped.
  - **A white square over Tom's Diner:** `neon_parallax` now draws its BaseColor mask through its GradientMap
    (`neon_parallax_image`). Checked on the material: the logo, not a square.
  - **Can't go higher, and falling through the highway:** the 16 m collision cap and its lid; the highway deck is at
    CP2077 z 30.9 m. To decide from a measurement of intermediate caps, since 400 m was 2.6x the triangles (section
    14). Not yet changed.
  - **Open:**
    - the glow beside a 15 m tube light (a capsule light becomes one point; which axis is unknown);
    - the covered cargo box colliding as its bounding box;
    - for the renderer: corner windows picking two rooms (the room hash includes the face) and SSR dither.
  - **The half tower seen from Northside:** the renderer's 277 m far plane. The hub sets no `distanceCull`, so the
    engine default of 6,000 x 1.732 applies, and the sphere slices tall buildings about 300 m away. The collision
    session will set an explicit distanceCull on the hub so `r_distanceCull` works; the renderer will measure 277 m,
    924 m and 1.85 km with hide distances on, for the owner to choose.
  - **Viktor's clinic, Misty's Esoterica and the Afterlife** are interiors, not converted yet (phase 4); all three
    are in `json_keep`. Outside spots are in the triage, and they're the natural first interiors.
  Owner: "do rebuild but do it in wherever makes sense in sequence". The order:
  1. Watson scene-2 with hide distances: v4, from today's parts, for the renderer's tests.
  2. Measure the collision cap at 24, 32, 48 and 64 m on the six cap tiles, then choose the cap.
  3. Tag 3:
     - fetch --ents (embedded templates and their apps), then fetch (their meshes) and a fresh tiles stage (a new ad
       pool);
     - all 89 tiles' parts with the fixes and native hide distances (about 1.5 h on two workers);
     - scene-3 tiled and the chunks;
     - the collision session's colchunks and a hub with distanceCull.
  4. Then the per-tile compare and drop on tag 3.
- **CP2077's hide distances, built** (owner: "Yes, use them"):
  - **Scene v4:** the instance record's last word is forceAutoHideDistance; the light box record's last 16 B are
    autoHideDistance, shadowFadeDistance, shadowFadeRange and allowDistantLight, in game units, 0 meaning none. The
    layout is unchanged.
  - **Converter:** the scan keeps them (instanced nodes per group, buffer.Unknown2 and Unknown3); parts carry
    `inst_hide`. `scene-hide` gives older parts a sidecar `inst_hide.npy`, matched by name and origin; entity meshes
    don't match and draw as before.
  - **The slice (scene-22 against scene-21) passed the gate fixed before the run:** byte for byte except the hide
    words. 258,442 of 275,447 instances have a distance (p10/p50/p90 26/90/357 m), and 8,151 of 8,879 lights
    (15/30/75 m).
- **Distance, decided (owner 2026-11-01: proxies "after tag 3").** CP2077 handles distance in three layers, all in
  its data (`look/review-20261101/cp2077_city_tech.md`):
  - full-detail meshes near the camera, each hidden past its own distance;
  - proxy meshes (merged, low-detail blocks and buildings) that start drawing closer than the detail hides, so the
    two overlap and nothing pops, out to 1.3-1.5 km;
  - sector levels loaded in distance bands.
  The first layer's data is in (scene v4). The second is the next converter addition after tag 3:
  - fetch the proxy meshes and the city-wide levels 4-6;
  - match proxies to the nodes they stand for (sourcePrefabHash and ownerGlobalId; the grouping rule to check);
  - the renderer crossfades in the overlap band, and the far plane moves to about 1.5 km.
  Until then, the far plane rises only as far as measured fps allows, with hide distances on.
- **Load radius, measured** (collision session, run 11: one walker on the first leg at 1,000 u/s, tag 1's chunks, 2
  cores, eviction at twice the radius):

  | Load radius | Chunk blocks peak / end | Private peak / end | Chunks peak / end | Shots start meeting unloaded boxes at |
  |---|---|---|---|---|
  | 9,600 u (today) | 805 / 712 MB | 1.06 / 0.98 GB | 18 / 14 | 9,680 u (258 m) |
  | 6,400 u | 656 / 547 MB | 0.93 / 0.82 GB | 11 / 9 | 8,551 u (228 m) |
  | 4,800 u | 504 / 490 MB | 0.78 / 0.77 GB | 9 / 7 | 5,961 u (159 m) |
  | 3,200 u | 394 / 394 MB | 0.67 / 0.67 GB | 7 / 5 | 3,764 u (100 m) |

  At every radius: 0 held frames, 0 false walls, worst server frame 0-1 ms. Tag 2 holds about 1.85x tag 1's chunk
  memory. Below 8,192 u (JA's hitscan range), a long disruptor shot can meet an unloaded box; a smaller radius also
  leaves less lead for a saturated disk or a fast mover. **Owner, 2026-11-01: "Keep 9,600"**; memory is to come
  from the compact chunk format and the spatial index instead.
- **The collision cap at intermediate heights** (`cp77/watson/cap-2.sh`, `.log`; the six cap-1 tiles with tag 2's xc
  arguments; collision triangles / prop boxes):

  | Tile | 16 m | 24 m | 32 m | 48 m | 64 m |
  |---|---|---|---|---|---|
  | -4_9 (Northside) | 103,235 / 1,672 | 209,788 / 4,281 | 405,101 / 6,131 | 664,827 / 9,357 | 771,016 / 11,168 |
  | -5_5 (Kabuki market) | 1,104,851 / 30,072 | 1,364,975 / 35,064 | 1,547,131 / 36,879 | 1,834,361 / 38,864 | 1,967,297 / 39,651 |
  | -6_4 | 473,459 / 13,102 | 754,970 / 17,010 | 946,419 / 19,462 | 1,206,835 / 21,610 | 1,377,936 / 22,561 |
  | -8_5 | 462,851 / 9,825 | 601,601 / 11,680 | 810,109 / 14,167 | 1,127,708 / 16,851 | 1,310,827 / 18,088 |
  | -8_9 | 292,440 / 7,122 | 446,146 / 9,869 | 520,292 / 10,749 | 549,873 / 11,290 | 576,064 / 11,458 |
  | -9_10 | 367,568 / 5,744 | 439,546 / 6,595 | 482,013 / 7,445 | 536,051 / 7,974 | 546,075 / 8,135 |
  | **six tiles** | 2,804,404 | 3,817,026 (1.36x) | 4,711,065 (1.68x) | 5,919,655 (2.11x) | 6,549,215 (2.34x) |

  The height is above the 12 m floor; the highway the owner fell through is 18.9 m over it. Server memory scales
  roughly with the triangles: tag 2's worst four players at 16 m were 3.67 GB.
- **No collision height cap (owner, 2026-11-01): "collision has to go wherever the level can take a player".**
  CP2077 has no height limit. Its collision is physics shapes (boxes, capsules, simplified meshes, `worldCollisionNode`)
  streamed by distance band: small deco within about 64-128 m, structure out to 300-600 m. Our 16 m cap was a
  prototype bound on render-derived collision. Tag 3 builds with `--col-height 400` (over Watson's tallest building,
  so the ring and lid close the area above everything): about 2.4-2.6x the triangles. That is paid for by:
  - residency (built);
  - the compact 32-bit collision structures (collision session, about -29%);
  - CP2077-style bands later (deco collision near only);
  - the walkable-preserving coarse LOD for walls and upper floors (in).
  `cp77_tiles.py` BUILD_ARGS carry it. Tag 3 (`cp77/watson/tag3.sh`):
  1. incremental fetches (embedded lamp templates and their meshes, the shutters);
  2. all 89 tiles' parts;
  3. scene-3 tiled;
  4. the chunks.
  It's queued after the renderer's far-plane bench.
- **Hide distances and the far plane, measured** (renderer session; Watson scene-2, one client at a time, 5 views;
  sheet `look/watson-p3-20261031/occ/far_sheet.png`):
  - The v4 loader is inert with hides off (identical to scene-1).
  - Hides on save 1.3-4.9 ms a frame; the lights alone save about 1.25 ms (clusters from 2.43 to 1.17 ms).
  - The far plane is cheap with hides on: 277 m to 924 m costs +0.04-0.6 ms on streets and +1.2 ms from 60 m up;
    1.85 km adds at most 0.6 ms more.
  - Hidden lights lose distant lit things (a bus shelter, highway lights). Keeping allowDistantLight lights gives
    the saving back.
  Owner, 2026-11-01: "Yes, build them": CP2077-style cheap distant lights (one point, no shadow, no haze, a fade at
  the hide distance). The view distance default: 924 m (distanceCull 20000), accepted as a stopgap (owner: "distancecull is
  kind of a meme until we get cp2077 distance meshes and related logic working"); the proxies after tag 3 are the real
  answer.
- **Far-city proxies: the design, before code** (2026-11-01; research `look/review-20261101/cp2077_city_tech.md`
  A3). The node types are worldGenericProxyMeshNode, worldBuildingProxyMeshNode (embedded textures),
  worldRoadProxyMeshNode, worldEntityProxyMeshNode and worldDestructibleEntityProxyMeshNode. Each has a mesh under
  `sectors\_external\proxy\<hash>\` (or `_proxyhelper`), `forceAutoHideDistance` (the far hide),
  `nearAutoHideDistance` (generic, building and road: hidden closer than this; 0 for entity proxies, which show
  beyond `entityAttachDistance` instead), `nbNodesUnderProxy`, `sourcePrefabHash`, and for entity proxies
  `ownerGlobalId`. Plan:
  1. **Scan:** emit proxies as placements (type `proxy`, near and far distances, hash, count, owner). They're skipped
     today.
  2. **Fetch:** the proxy meshes, incremental, and sector levels 4-6 for the far ring (`sector_regex`).
  3. **Groups:**
     - an entity proxy replaces its owner entity (`ownerGlobalId`), and the entity's meshes hide past
       entityAttachDistance;
     - a generic, building or road proxy replaces the mesh nodes sharing its sourcePrefabHash inside its bounds,
       when their count matches nbNodesUnderProxy. Those nodes then hide at the proxy's near distance plus the
       overlap. When the count doesn't match, draw no proxy and keep the nodes.
  4. **Scene v5:** the instance's flags word becomes its near distance (f32, 0 = none), with a proxy bit
     elsewhere; to agree with the renderer.
  5. **Renderer:** draw an instance only between its near and far distances, crossfade in the overlap band, and use
     proxies as middle and far cascade casters. Then the far plane goes to about 1.5 km.
  6. **Gate:** the aerial and long-view image checks for holes and doubles, and timing at 924 m and 1.5 km.
- **Proxies, step 1 done (code):** `scan` keeps CP2077's proxy nodes as `placements.json` `proxies` (`proxy_rec`:
  mesh, app, type, m, near (nearAutoHideDistance, or entityAttachDistance for entity proxies), far, count, prefab,
  owner), apart from the placements, so no build takes them yet. Twelve sectors gave 429 proxies (357 generic, 58
  entity, 14 destructible entity), all with a near distance and 72 with an owner. Next: the Watson rescan, fetching
  the proxy meshes (a `fetch` over the proxies), the replacement groups, scene v5's near distance, and the renderer.
- **Proxies, the scan's result and the matching keys** (2026-11-01). The Watson rescan (`cp77/watson/scan-proxies.log`)
  kept 79,452 proxies: 60,573 generic, 13,857 entity, 1,950 building, 1,848 road and 1,224 destructible entity. For
  the groups:
  - mesh nodes carry `sourcePrefabHash` (worldStaticMeshNode, worldEntityNode), but the scan's placements don't keep
    it yet; to add to `emit` (and to the entity records) before the matching;
  - an entity proxy's `ownerGlobalId.hash` is not the nodeData record's `Id`: 0 of 317 owners matched 77,926 record
    Ids on sectors -16..-18. The global node ID is derived some other way, still unknown. Until then, entity proxies
    are matched by position (the entity whose origin sits at the proxy's, with a related mesh), checked by the share
    that matches one-to-one.
  Next: placements keep `prefab`, then the matching analysis offline (match rates by type), then fetch only the
  proxies that match.
- **Proxy matching, measured offline** (`proxy_match.py` in the session scratch; placements now keep `prefab`).
  - **Entity proxies, by an entity origin within 1 m:** 13,682 of 15,081 match exactly one entity, 1,388 several, 11
    none. Position is enough.
  - **Generic, building and road proxies, by same-prefab nodes within a radius of the proxy's origin:** only 5-11%
    match nbNodesUnderProxy exactly (best at 32 m). Most find more, since a prefab hash is shared by every copy of a
    prefab. They need the proxy mesh's bounds (nodes inside them), so the proxy meshes come first: 5,029 unique,
    `fetch --proxies` (new), running.
- **Proxies: no matching, by CP2077's own distances (decided 2026-11-01).** The proxy meshes are fetched (`fetch
  --proxies`, and `--lods`; 5,027 of 5,029). Matching by placed bounds, same prefab inside the proxy's box, was no
  better: 14,635 of 64,371 generic, building and road proxies exact, 30,145 with more nodes (copies overlap), 19,591
  with fewer (nodes our scan doesn't carry: foliage, decals, skipped types). CP2077's engine doesn't match either:
  each full mesh hides at its own forceAutoHideDistance (in scene v4), and each proxy draws between its own near and
  far distances; the overlap is authored. So:
  1. Every proxy goes into the scene with its near and far distances, unmatched.
  2. Entity meshes, the ~5% of instances with no hide distance, take their entity proxy's entityAttachDistance as
     their hide distance: entity proxies match an entity within 1 m one-to-one in 91% of cases.
  3. Scene v5: the instance record's flags word carries the near distance (f32, 0 = none). The renderer draws an
     instance only between near and far, crossfading in the overlap.
  4. Proxies as middle and far cascade casters, then the far plane to about 1.5 km.
  The parts: a proxy part per tile (or proxies added to the scene parts), merged into the tiled scene.
- **Named spawns** (owner: "can we put a spawn outside of misty's and outside of afterlife"): AREAS 'named_spawns'
  (CP2077 metres and yaw), added in the base build beside the picked spawns. Tag 3's base.glb was rebuilt with them
  (the old one kept as `tiles/3/chunks-base-16spawns.glb`; the original 16 unchanged): spawn_mistys_esoterica at game
  (-10106, -39468, 190) facing 72, spawn_afterlife at (-7275, -44812, 420) facing 90. The collision session builds
  hub col-6 and kit hub_col-6 from it.
- **Tag 3's collision, compiled and measured** (collision session; `look/collision-20261030/tag3/`).
  - **colchunks-5:** 89 chunks, 7,990 MB of BSP (tag 2: 3,875 MB). Hub col-5 with distanceCull 20000; its spawn
    gate passes. Kit `build/cp_watson_hub_col-5` (engine 970dbb8c, rd-vulkan 7fb72585, scene-3) passes its smoke:
    peak 16.5 GB with all chunks resident. Not installed.
  - **Resident cost on the dedicated server:** dense4 (34 chunks) 4.95 GB (tag 2: 3.67 GB); all 89 chunks 7.78 GB
    (tag 2: 5.63 GB for 82).
  - **The settled walk:** 17 loads, 22 evictions, peak 3.18 GB; 0 violations, holds or false walls; worst frame 1 ms.
  - **Long sweeps:** 0 of 1,280 go past their all-loaded twin; the shortest that differs is 9,887 u.
  Collision above the 402 m lid (three tall-building chunks up to 506 m) is unreachable; trimming it is a converter
  todo. Hub col-6, with the two named spawns, is next.
- **Collision bands, designed and shelved** (collision doc section 28; this session's ruling). The near/structure
  split by CP2077's rule saves only about 0.6-0.8 GB of dense4's 4.95 GB: boxes are 2.6% of brushes, and pieces
  under 6 m are about 23%, so structure is about 77%. Next: fewer, larger structure brushes (merging connected
  coplanar triangles into convex polygons, one brush each), measured offline on dense4's tiles first. The collision
  session reports how gltf2map builds brushes today.
- **Where the structure brushes come from** (collision doc section 29). gltf2map already merges coplanar
  edge-neighbours (up to 12 corners, within 0.1 u). dense4 has 19.76 M brushes from 33.7 M triangles, and 57% of
  brushes are single triangles: slopes, bevels, pipes, roofs, roads and terrain, mostly small and finely
  tessellated. Big facades already merge. No exact merge cuts it: 32 or 64 corners give the same count, and exact
  edge grouping gives +0.9%. Next, approved:
  1. Export tile -4_7 with `--col-cell 0` (no 0.1 m vertex grid) for the census; the grid bends near-flat pieces by
     up to about 1.9 u, and the ceiling is about 12%.
  2. Coarser structure collision (`--col-tris` 2000 and 1000 under `--col-walk-lod`) on dense4's tiles, against a
     geometric gate fixed first: levelcheck falls + void no worse than tag 3's, missing within a tolerance, no
     stuck spawns, a wall sweep corpus.
  Both come after the playtest path.
- **Tag 4: Watson with CP2077's proxies** (`cp77/watson/tag4.sh` and `tag4b.sh`; scene parts only, collision is tag
  3's). `build/cp_watson_scene-4` (v5): 1,672,325 instances of 25,611 meshes (scene-3: 1,614,123 of 21,659), 25,092
  materials, 12,248 images. Proxies carry near and far; 10,809 entities hide at their entity proxy's attach
  distance. `cp77_tiles.py` now passes `--proxies` to the tiles stage and has `--scene-only`. The renderer's v5
  loader and crossfade are gated on it next. The vertex grid stays: without it, -4_7 has 2.25x the brushes (collision
  doc section 29). The coarse-structure arms (`arms-ct.sh`, tags ct2000 and ct1000) are building.
