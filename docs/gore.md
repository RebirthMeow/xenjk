# Gore

Client-side blood, gibs and dismemberment for xenjk. Owner direction (2026-11-26): a
gib/dismember/blood mod, cvar-gated. Decals and dismemberment come first, the
flowing blood pools (studied from a Cyberpunk 2077 mod, not copied) later.

## Phase 1: blood (done, not installed)

Flesh hits throw ballistic drops (`cgame/cg_gore.c`). Each drop flies under gravity, passes
through bodies and leaves a mark where it lands: a splat on floors and ceilings, a run that
drips down the wall on walls. Hits also leave blood on the body (ghoul2 skin gore). Marks are
alpha blended and lit from the light grid, dry from red to brown, and fade at the end of their
life. It is all local to the client. The server is unchanged, and so are the protocol and the bots.

| Source | Event | Blood |
|---|---|---|
| Saber on a person | `EV_SABER_HIT` (parm 1/2/3/16) | spray along the cut, drips below, body mark |
| Bolts, disruptor, rockets on a person | `EV_MISSILE_HIT`, `EV_DISRUPTOR_HIT` | exit spray behind the target, a little back, body mark |
| Explosive deaths | `EV_OBITUARY` with a splash MOD | burst from the chest |

Droids, vehicles and other machines (`ET_NPC` classes) don't bleed. DEMP2 and the stun baton
are electric, so they make no blood.

Cvars (client, archived):

- `cg_gore` 1: 0 off, 1 blood, 2 heavy (twice the drops, bigger marks)
- `cg_goreSaber` 1: 0 keeps saber wounds cauterised (stock sparks and burns only)
- `cg_goreLook` 2: the art set, 1 the first flat look, 2 the less cartoonish one (below)
- `cg_goreTime` 90: seconds a blood mark stays on the world
- `cg_goreDebug` 0: prints sprays and landings, and enables `gore_spray [count]`, which throws
  blood at the surface in view

Engine changes that go with it:

- `MAX_MARK_POLYS` goes from 256 to 1024, and marks have a per-mark lifetime. This also fixed
  a stock bug: `CG_AddMarks` wrote 0..1 floats into byte vertex colours. Blood marks take their
  own path, and the stock marks are left as they were.
- Renderer `DEFAULT_MAX_POLYS` goes from 600 to 4096 and polyverts from 3000 to 20480 (rd-vulkan,
  rd-vanilla, rd-rend2), because the cvar can only lower the cap. **The new cgame and the new
  renderer must be installed together.** With the old 600 cap, a bloody fight hits
  `r_max_polys` and drops marks and effect particles.

Look 2 (owner: v1 "looks a little cartoonish"; redesigned 2026-11-26 after a side-by-side with a
Cyberpunk 2077 gore mod's decals). Real spatter is hundreds of specks, clumps and thin drips
covering about a metre, not a few solid shapes, so a landing drop no longer stamps its own small
mark. It paints one large decal for the spot, and drops landing near a fresh one (2 s) add nothing:
- floors: a mist of specks in clumps, slanted drops with tails on the rim (about 1 m across);
- fast drops at a slant: a directional spray fanning the way they flew (about 2 m long);
- walls: a ragged speckled smear with 6-14 thin drips running down, some ending in a bead.

Spot size follows the drop (0.6-1.5x), and the colour is a little darker than v1. The owner accepted
this look (2026-11-26); the known gap is that our specks are rounder and crisper than the
reference's soft, streaky ones, a later tweak. While fresh (30 s)
each mark has a second, additive pass for the wet shine: a faint highlight of the grid's main light
and a grazing-angle reflection. That pass doubles the polys of fresh marks, and its highlight is
per vertex, so it stays faint (a strong one turned whole drops pink).

`cg_goreLook 3` is a slot for local reference art (`gfx/xen_gore/ref_*`, placed at the art's own
size); nothing we ship fills it.

Art: `zz_xenjk_gore.pk3` comes from `python tools/gore/make_gore_assets.py [--preview]` (output in
`build/gore/`). It is procedural, so we own it. It holds the v1 set (4 splats, 3 wall runs, a body
mark, a drop sprite) and the v2 set from `tools/gore/gore_v2.py` (4 floor mists, 3 wall drips, 3
directional sprays, a body mark, each world mark with a `_wet` shader). Without the pack the shaders don't register and the blood is skipped.

Verification (2026-11-26, local record `.codex-research/gore-20261126/`):

- `gore_check.py` runs the installed 64-bit client with this cgame and the pack in a private
  home, hidden and muted, on localhost.
- It kills frozen stormtroopers with a blaster, a saber and a rocket, then sprays four walls.
- Checked on mp/duel1 (Bespin) and mp/ctf2 (Hoth): blood on floors, walls and bodies, and wall
  runs pointing down.
- The renderer poly cap could not be tested in game without installing; that test is the
  installed 600-cap renderer.

## Phase 2: dismemberment and gibs (implemented)

Stock JKA cuts one limb on a saber or heavy-melee kill, and only when `g_dismember` (a % chance,
default 0) and `cg_dismember` (default 0) allow it. The cut smokes and the cap is charred with
glowing embers. Q3's gib code is still there but commented out. Phase 2 keeps JKA's own approach
(the body's own ghoul2 limbs fly as `playerlimb` entities) and makes it bloody and frequent.

Server, `g_gore` (default 1, archived; the stock `g_dismember` keeps its meaning when it is 0):

- Saber and heavy-melee kills always dismember (as `g_dismember 100`), still where the blade hit.
- Explosive kills (splash, rockets, thermals, mines, detpacks, concussion) of 40+ damage cut 1-2
  limbs, 2-4 at 100+, thrown away from the blast and upward, harder with more damage.
- A disruptor headshot that kills takes the head. A fully charged shot still disintegrates the
  body as in stock, which reads better than a headless body that then vanishes.
- Deaths only: live limbs are never cut, since that would change gameplay and the bots.

Client, `cg_gore` with `cg_goreSaber` (both default 1):

- `cg_gore 1` shows dismemberment in full when `cg_dismember` is 0, so the defaults alone work.
  Set `cg_dismember 1` for less, or `cg_gore 0` for stock.
- The cut and the stump spray blood instead of smoke, and the stump gets a blood mark. Stumps
  pulse like a slowing heartbeat for 5 s with a dribble between pulses (`GORE_STUMP_LIFE`); a
  severed limb trails drops for 2.5 s of flight. All of it lands as phase 1 marks.
- The stock cap shader (`models/players/stormtrooper/caps`, which nearly every model's cap
  surfaces use) is remapped to `gfx/xen_gore/cap`, a tiling raw-flesh texture from
  `gore_v2.py`. The remap happens at map load, so changing the cvars needs a map reload or
  `vid_restart`.
- `cg_goreSaber 0` keeps blood decals but restores the cauterised stock look (smoke, charred caps).

Not done here: a mark growing under the corpse belongs to the blood pools work. Stump pulses are
easier to judge in motion than in stills.

## Phase 3: blade drip and footprints (implemented)

- **Blade drip.** A saber that cut someone (`EV_SABER_HIT`, attacker from `otherEntityNum2`)
  drips for 4 s: drops fall from points along each lit blade, fast at first, slowing as it runs
  dry, and a hard swing flings drops off the tip. `CG_Gore_Blade` runs from
  `CG_AddSaberBlade`. It is subtle by design: the drops land in the kill's own spatter unless
  you walk away while it drips.
- **Footprints.** Floor landings are remembered for 40 s (`goreFloor`). A footstep anim event
  (`CG_PlayerFootsteps`, now also run with `cg_footsteps 0` while `cg_gore` is on) that lands in
  one loads that foot's sole; the next 10 steps (15 at `cg_gore 2`) print a shoe sole, heavy,
  then thinner, then patchy (`print_[lr]1..3`, painted by `gore_v2.footprint`). Prints age and
  fade like other blood marks. Feet are set 4 units apart where a model's foot bolts come back
  on the centre line.
- **Dropped (owner 2026-11-26):** screen blood.

## Meat chunks (implemented)

When `g_gore` bursts a body (an explosive kill of 40+ damage, `G_GoreDeath`), the server sends
a temp `EV_GIB_PLAYER` naming the body and how many limbs it threw; stock cgame ignores it.
`CG_Gore_Gib` throws 5-13 lumps of flesh (half as many again at `cg_gore 2`) as `LE_FRAGMENT`
local entities: they tumble, drip blood in flight, splat where they first land, bounce dully and
lie there for 12-18 s. The models are six noise-displaced, squashed icospheres from
`tools/gore/gore_chunks.py` (MD3, about 2-6 units in radius), skinned with a procedural torn-flesh texture
(`gore_v2.chunk_tex`) and a faint view-dependent wet gloss. `gore_spray <drops> <chunks>` (with
`cg_goreDebug 1`) throws them at the surface in view.

NPCs get no `EV_OBITUARY`, so until this event the explosive blood burst only ever fired for
players and bots. `EV_GIB_PLAYER` now carries it for NPCs as well.

## Blood pools (implemented, look 3)

Look 3 is the accepted look (owner, 2026-11-26).

`cg_gorePools` (default 1, with `cg_gore`). A body that has lain still for 0.7 s bleeds a pool
from its lower back (`lower_lumbar`; the entity origin when the bolt is missing or far off).
`cg_gore_pool.c`:

- **Floor capture.** A 32 x 32 grid of 3-unit cells (~2.7 m) round the body, one trace down
  per cell, 96 traces a frame across all pools. A cell is floor, wall (solid, steep, more than
  10 units up, or no-marks) or sink (no floor within 30 units, or water). Depths are physical
  (a unit is ~2.7 cm): a pool is about a millimetre deep, so the floor is taken as flat to a
  fraction of that.
- **Flow.** Virtual pipes: each floor cell pushes blood to its eight neighbours in proportion to
  the difference in surface height (gain 10, 0.9 of last step's flow kept), 30 steps a second;
  blood under ~0.8 mm moves slower. Blood into a sink or off the grid is gone. A dry cell takes
  blood only once the pool stands higher than its hold (~0.5 mm, a smooth noise x0.6-1.7), and
  the hold is 2.5x beside a single wet cell down to 0.2x inside a notch of eight: a stand-in for
  surface tension, so the pool advances with a thick, rounded front, stops when it can no longer
  beat the hold, and grows lobes but no spokes. A drained cell keeps a ~40 micron film. Smoothing
  of the blood above the film runs between wet cells only. The body pours 90-130 cubic units
  (~2.5 litres, x1.5 at `cg_gore 2`) over 8 s, hard at first; the pool settles in ~16 s at about
  1.5 m across. Tuned offline with the same maths in numpy.
- **Drawing.** One quad per cell, corners shared, at the floor's height (none bridging a step of
  more than 3 units), through `R_AddPolysToScene`. The edge is cut as geometry: each corner has
  a field (its coverage against a smooth world-space noise, lowered as the pool fades), and rim
  cells are clipped where it crosses zero, so the edge is crisp and ragged at any MSAA and a
  fading pool shrinks raggedly. (Look 1 alpha-tested a noise texture instead; under MSAA
  alpha-to-coverage turned that into a dithered, fuzzy edge.)
- **Colour.** The body multiplies the floor (`GL_DST_COLOR GL_ZERO`), so it is lit exactly as the
  floor is on any map (look 1 lit it from the light grid, which misjudged lightmapped floors:
  on Hoth's red floor the pool read as a shadow). Blood is a red filter: light passes through a
  thin layer, red most and blue least, so the rim and a drained film are a clear red stain;
  from about half a millimetre it is an opaque, dark pigment that dries like a mark. The filter
  is worked out in linear light against a floor of 0.25 albedo, then put back in gamma. Because
  it multiplies, a thick pool still shows the floor's texture faintly. The tiling texture
  (`gore_v2.pool_tex`) adds a mottle and darker, drawn-out clotting streaks.
- **Gloss.** An additive pass reflects a made-up room (`gore_v2.pool_env`: a dark dome with a few
  soft lights, its centre straight up). cgame gives it texture coordinates from the reflected
  ray of the pool's own smoothed surface normal (floor bumps and the rim's meniscus distort
  it), weighted by Fresnel and the grid's light, plus a soft highlight of the grid's main light.
  The renderer's `tcGen environment` was tried first: it uses each cell's own flat normal, and
  the floor's slight tilts made the reflection jump from cell to cell. The blood's thickness is
  exaggerated in that normal, so the rim's meniscus catches the light. Sharp per-vertex
  highlights read as squares, so sharp shapes are left to the texture. Full gloss for 40 s,
  matt by 100 s. A pool lasts twice `cg_goreTime`.
- Up to 8 pools; a new one within 24 units of a recent one is skipped (a player's body copy, a
  heap of bodies). Wet pools note their area for footprints.
- `gore_pool` (with `cg_goreDebug 1`) starts a pool on the floor in view; `cg_goreDebug 2`
  leaves out the shine.

**Renderer fix (rd-vulkan `RB_SurfacePolychain`).** Polys carried no normals, and the
character-shadow term normalises the surface normal: under a character's shadow a poly came out
black. Polys now get their fan's plane normal. Blood marks and pools both need it, so it ships
with them.

## Later

Pools fed by each wound rather than one per body, pools that merge, softer rounder specks in
the spatter (owner, kept for later). Ideas for pools come only from `meow/BloodPoolSimulation`
(look 3 took its thick wetting front and its light-through-thin-blood colour, both written anew)
(all rights reserved, so no code or textures from it).
