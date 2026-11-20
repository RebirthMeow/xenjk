# Character texture memory: one budget (design and measurements, 2026-11-09)

Branch `claude/char-vram-budget-a69081`, off `renderer/install68-models` 6fe7bb2f (the installed renderer line).
Answers the 2026-11-09 character rendering review, textures lane: TX-1, TX-2, TX-3, TX-4, TX-5, TX-12
(`.codex-research/review-charrender-20261109/textures.md`).

Status words: **design** = written before the code; **built** = in the branch; **measured** = numbers below from this
PC; nothing here is installed.

## 1. What is reused

`renderer/vram-budget` 657bd66f (2026-11-09, on `baseline/1`, the Watson kits' 64-bit renderer line) already holds a
large part of this, built for the city's textures. That line's `vk_stream.cpp` also carries earlier stream-renderer
work (H34, H37) that never reached the installed line. This branch takes from it, unchanged:

| piece | from | covers |
|---|---|---|
| `vk_vram.cpp`: device-memory ledger, the budget from `VK_EXT_memory_budget`, `vkmem` | 657bd66f | TX-3 (budget) |
| pool = budget less the rest of the renderer less `r_vramReserveMB`, read every frame | 657bd66f | TX-3 (pool size) |
| shadow views (`sunCascade`, `charShadow`) ask for nothing | H34 | TX-1 |
| failed builds and reads retried (5 s, 10 s, 15 s), failed for the map only at the fourth | H34 | TX-5 |
| built images as ranges of 64 MB blocks, not one allocation each | H34 | TX-12 |
| images that do not stream admitted against the budget; chunk allocation falls back instead of `ERR_DROP` | 657bd66f (A) | the 2026-11-09 crash |
| model, hair and cloth buffers in system memory past the budget | 657bd66f (V3) | the same |

`vk_stream.cpp` and `vk_vram.cpp` are that branch's files, so the later merge with `baseline/1` sees only this
branch's additions. The scene hooks in them (`R_StreamWantImage`, `R_StreamSceneImages`) have no caller on this line.
Not taken: the count-then-scan cull and the perf items of the same commit (render scene only).

What `renderer/vram-budget` does not have, and this branch adds (sections 2.2 to 2.5): an order for which live
image steps down, hysteresis, compaction, a release path for textures that cannot stream, the pin floor, and a way
to force a smaller card's budget.

## 2. Design

### 2.1 One pool, sized from the budget (TX-3)

- **Budget**: the device-local heap's `heapBudget` (`VK_EXT_memory_budget`), which the OS already reduces by other
  processes' use. Without the extension: the heap's size, and use from the ledger. `r_vramBudgetMB` (new, 0 = off)
  caps it, to run this PC as a smaller card.
- **Pool** = budget − (process use − the pool's own images) − `r_vramReserveMB` (1024), at least 128 MB, at most
  `r_streamPoolMB` when that is set (it was the size; it is now a cap, default 0 = none).
- Everything above a texture's tail counts against it: streamed mips, and now also the textures that cannot stream
  (2.3). Tails (256 texels across and smaller) stay outside, in the image chunks, as before.
- Old rule on this line: half the largest device-local heap, whatever else was loaded (6 GB of this card, 3 GB of a
  6 GB card), with only never-drawn images ever leaving.

### 2.2 Giving memory back under pressure, in the order of what the screen loses (TX-3)

"Over" means process use plus the reserve is above the budget (or the pool above its cap). While over, each frame
takes the first step that still has something to give:

1. **Idle images to their tails**, least recently drawn first. Streamed files: not drawn for 0.5 to 1 s (as before).
   Whole-loaded textures (2.3): not drawn for `r_streamWholeIdle` seconds (10), since coming back costs a decode.
2. **Compaction.** Freed ranges inside 64 MB blocks are not free VRAM. When the free ranges add up to the excess, the
   emptiest block's images are rebuilt in the other blocks' free ranges (a GPU copy at the same level: nothing
   visible) and the block goes back to the driver.
3. **Surplus levels.** A drawn image two or more levels sharper than the screen asks is rebuilt one level above what
   it asks (GPU copy, no read). The spare level is the hysteresis for approach and retreat: a 2x change in distance
   needs no rebuild.
4. **Squeeze.** Only when 1 to 3 leave nothing: a pool-wide bias, one step per 250 ms at most.
   Step 1 drops the spare levels (still nothing the screen shows). Step 2 and up cost one visible level each, on
   every unpinned image alike, up to 4 levels.
5. Whole-loaded textures idle for less than `r_streamWholeIdle` go to their tails with step 4's first step.

**Hysteresis (no pops while still).** The squeeze only rises while over. It falls one step at a time, not sooner
than 5 s after its last change, and only when the levels the lower step would restore fit with 10% of the pool to
spare. Standing still, wanted levels do not move, so the estimate does not move: the pool settles at the smallest
squeeze that fits and stays. `r_streamStepDown 0` turns steps 2 to 5 off (only idle images leave, the old behaviour).

The local player's model stays pinned and takes no part in 3 to 5.

### 2.3 Textures that cannot stream: counted, released and reloaded (TX-2, the picker)

Today a player texture that is not a stored, mipmapped DDS (a deflated DDS, or PNG, JPG, TGA: 114 of 238 installed
folders plus five packs) loads whole into the shared image chunks and stays until `RE_Shutdown`. That is the
2026-11-09 crash (nine heroes, +2.3 GB of chunks), and it is why the picker has no texture cap.

- A mipmapped texture under `models/players/` larger than the tail that cannot stream becomes a **whole-loaded
  streamed image** (`r_streamWhole 1`, latched): its tail goes to the chunks as for any streamed image, and its
  full chain is uploaded once, at registration, into a pool block, when the pool has room for it (else the
  sharpest level that fits, else the tail alone).
- From there it steps down and drops to its tail like any other image (GPU copy).
- **Coming back** needs the file again: one whole-loaded image per frame, in `RE_BeginFrame`, is read and decoded
  as the loader did the first time and uploaded at the level the screen asks. The cost is that texture's first-load
  cost again (an estimated 80 to 150 ms for a 2048 PNG; not timed here), paid only after the pool was short and the
  texture was not drawn for 10 s, or under the squeeze.
- **The picker's cap is the pool.** Browsed characters' textures become idle once the stage moves on. They are
  the first to leave when the next character needs room. No count cap is needed for textures and none is added;
  `ui_picker.c` is untouched.
- The admission rule taken from `renderer/vram-budget` (drop top levels for good when the budget is spent) no longer
  applies to these textures: they are admitted by the pool and can return.

Not covered, by design: a browsed model's meshes, hair and cloth buffers (nothing frees a model mid-map; model
residency is its own task), and the decode hitch itself (the offline fix is cooking the 114 raw folders and
re-storing five packs: review TX-2 and TX-6, plan C).

### 2.4 Shadow views (TX-1)

`R_StreamWantSurface` returns for `sunCascade` and `charShadow` views, as on `baseline/1`. A character shadow tile
measured from the caster's own origin asked for the same detail as the character standing 60 units from a 1080p
camera, for up to 8 casters, at any distance. Not behind a cvar: a depth-only view samples at most the alpha of a
cutout, at the tile's resolution, and the main view's request still holds whenever the character is on screen.

### 2.5 The local player's pin (TX-4): in, small

- `r_streamPinSize` (0 = full size, the present behaviour and the default): pinned textures are held at least this
  many texels across, and sharper only as the screen asks. Recommended 2048 for the 6 GB profile.
- `r_streamPinTeamSkins` (default 0): the `red` and `blue` skins are still warmed, but not pinned. They stream by
  on-screen size like every other player's when a team game draws them. 1 restores the old pin.

### 2.6 Build allocations (TX-12): in, as reused

Blocks of 64 MB with first-fit ranges, as on `baseline/1`. New here: an allocation can be told to avoid a block
(compaction, 2.2 step 2).

### 2.7 Cvars

| cvar | default | meaning | visible? |
|---|---|---|---|
| `r_vramBudgetMB` | 0 | cap on the VRAM budget (0 = the driver's); for runs as a smaller card | yes when set low |
| `r_vramReserveMB` | 1024 | VRAM the pool leaves free under the budget | no |
| `r_vramFloorMB` | 256 | free VRAM under which a non-streamed texture loads with top levels dropped | only past the budget |
| `r_streamPoolMB` | 0 | cap on the pool (was its size; 0 was half the heap) | only when set low |
| `r_streamStepDown` | 1 | live images give levels back under pressure (2.2 steps 2 to 5) | step 4 only |
| `r_streamWhole` | 1 (latched) | player textures that cannot stream live in the pool (2.3) | no (a reload hitch under pressure) |
| `r_streamWholeIdle` | 10 | seconds undrawn before such a texture may go to its tail | no |
| `r_streamPinSize` | 0 | pin floor in texels (0 = full size) | no at 2048 and 1080p; yes in a close-up while it sharpens |
| `r_streamPinTeamSkins` | 0 | pin the team skins too | no |

### 2.8 Expected savings (before measuring)

| item | expected | basis |
|---|---|---|
| TX-1 | 50 to 300 MB per distant shadow caster, up to 8, on maps without sun cascades | review, folder sizes |
| TX-2 | the picker's growth bounded by the pool instead of 170 to 530 MB per raw or deflated character for the map's life (2 to 4 GB over ten) | review; the 2026-11-09 run's +2.3 GB over nine |
| TX-3 | on a 6 GB card the pool follows what is free (about 2.5 to 3.5 GB in a stock map) instead of a fixed 3 GB beside everything else; distant characters' surplus levels returned under pressure | design |
| TX-4 | team skins with their own maps: the folder's size again per skin (median 75 MB, p90 319 MB); pin floor 2048: 64 to 320 MB per 8192 or 16384 map of the player's own model | review |
| TX-12 | no MB; one allocation per 64 MB instead of per image | review |

## 3. Measurements

This PC (GeForce RTX 3080 12 GB: device-local heap 12,093 MB, the driver's budget 11,325 MB). Functional and memory figures from
`tools/char_vram_check.py` clients (installed exe, modules and packs; only the renderer differs; hidden, net off).
**Nothing here is a timing result.** Client VRAM is the process's dedicated GPU memory from the Windows counters;
pool held is what streamed and whole-loaded textures hold above their tails.

Renderers: **before** = `renderer/install68-models` 6fe7bb2f (sha256 dd9ef751...); **after** = this branch at
67b142c2 (7c15a3e7..., `.codex-research/char-vram-20261109/dll/after5`); **after+TX-5** = 373e1a30 on
`claude/char-fixes-vram` (62b31f7d..., `.codex-research/char-fixes-20261109/dll/fix_vram`). Runs:
`.codex-research/char-vram-20261109/runs/` (2026-11-09) and `.codex-research/char-fixes-20261109/runs/` (2026-11-10);
tables with `python -I tools/char_vram_report.py table <tags> [--work <dir>]`.

### 3.1 The picker (TX-2, TX-3): browsing 26 rows, every second one also its second outfit

| run | renderer | settings | client VRAM after 26 rows | pool held | whole-loaded | at tail |
|---|---|---|---|---|---|---|
| P_before | before | defaults | 6,873 MB | 1,204 MB | (in the chunks, never released) | 926 |
| gp_after | after | defaults | **4,698 MB** (-2,175, -32%) | 2,094 MB | 1,682 MB | 939 |
| gp_after6 | after | `r_vramBudgetMB 5600` (a 6 GB card) | 4,570 MB | 2,094 MB | 1,581 MB | 890 |

As a 6 GB card the picker stays under budget without squeezing (10 images moved to empty a block, 1 trimmed).

### 3.2 Eight heroes (xenjk_showcase deck, `r_sunShadows 0`: character shadow tiles), far, near, close, far again

| run | renderer | settings | client VRAM, far 20 s | pool held | squeeze / trimmed / moved / reloads |
|---|---|---|---|---|---|
| x8_before | before | defaults | 3,609 MB | 564 MB | - |
| g8_after | after | defaults | **2,975 MB** (-634) | 1,556 MB | 0 / 0 / 0 / 0 |
| g8_after6 | after | `r_vramBudgetMB 5600` | 2,975 MB | 1,556 MB | 0 / 0 / 0 / 0 |
| sq_b3300 | after+TX-5 | `r_vramBudgetMB 3300` | **2,271 MB** | 1,033 MB | 0 / 30 / 44 / 0 |
| sq_b2600 | after+TX-5 | `r_vramBudgetMB 2600` | **1,575 MB** | 299 MB | 0 / 37 / 81 / 63 |
| g8_tight | after | `r_streamPoolMB 300` | 1,759 MB | 295 MB | 2 / 55 / 0 / 40 |
| v_pool1 | after | `r_streamPoolMB 1` | 1,247 MB | 1 MB | 2-3 / 0 / 0 / 3 |

- **The budget holds.** With the budget forced to 3,300 and 2,600 MB the client settles at 2,271 and 1,575 MB:
  the budget less `r_vramReserveMB` (1,024) less a few MB. Steps 2 and 3 of 2.2 (compaction, surplus levels) were
  enough at 3,300 MB; at 2,600 MB whole-loaded textures also went to their tails and came back (63 reloads, each a
  decode: the hitch 2.3 expects under pressure). The pool-wide squeeze (step 4) stayed at 0 in both; it acts only
  under the pool cap (`g8_tight`, `v_pool1`).
- **No mip pops while still.** The pop check (`char_vram_report.py pops`; mean absolute difference between still shots
  of the far view, sharpness of the figures' band): sq_b3300 1.42 and 1.22, sq_b2600 2.04 and 1.08, against
  1.82 and 1.04 in g8_after and 1.76 and 1.45 in x8_before (the frozen NPCs' idle motion). Sharpness 8.4 to 8.9 in
  every arm. Sheets: `.codex-research/char-fixes-20261109/pops_squeeze.jpg`, `.codex-research/char-vram-20261109/pops_*.jpg`.

### 3.3 Character shadows (TX-1)

Eight heroes, far view, `r_charShadows` 1 (default; character shadow tiles, no sun cascades) against 0:

| renderer | shadows on | shadows off | the shadows' cost |
|---|---|---|---|
| before (x8_before, tx1_before_ns) | 3,609 MB, pool held 564 MB | 3,077 MB, 28 MB | **532 MB** (536 MB of it pool) |
| after+TX-5 (tx1_fix_cs, tx1_fix_ns) | 3,039 MB, 1,569 MB | 2,975 MB, 1,556 MB | **64 MB** (13 MB pool; the rest the atlas) |

Before, each distant caster's shadow tile asked for near-full detail; the shadow views now ask for nothing.

### 3.4 The local player's pin (TX-4)

`r_streamPinSize 2048` against 0 (full) changed nothing measurable for either model tried: Tifa (gpin_tifa_full /
_2048: 1,121 MB both; her maps are at most 2,048 across) and Roy Mustang Valentine (pin_roy_full / _2048: 1,572 MB
both). Roy's two 16,384 maps (256 MB each) are read by FMA stages marked `nomipmaps`, so they load whole, one level,
outside streaming, and no pin applies to them (review finding TX-21). The pin floor helps only a mipmapped map over
2,048 on the player's own model; none of the installed heroes tried has one.

### 3.5 TX-5 (on `claude/char-fixes-vram`)

After `fs_restart` (handles stale), then sharper levels wanted (`r_streamBias -4` then 2): the branch tip left 171
textures failed for the map; 373e1a30 reopened all 171 and ended with 0 failed, 333 of 333 above their tails.

### 3.6 Not measured

A frame-time cost of any of this (no timing runs); a real 6 GB or smaller card (only the budget cap); the reload
hitch's length.
