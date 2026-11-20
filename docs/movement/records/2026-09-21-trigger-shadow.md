# Trigger shadow disagreements: endpoint contacts measured, swept crossings unresolved

Answers the [trigger shadow handoff](2026-09-21-trigger-shadow-handoff.md).
A default-off shadow diagnostic measured what native contact would have decided
at the same simulated frame. **No admission rule changed**, nothing was
installed, and no speed was claimed or measured.

Protocol, driver, analyser and evidence in
`.codex-research/trigger-shadow-20260921-a/`; summaries
`summary-shadow-1.json` and `summary-phases-1.json`.

## The four boundaries that differ

`TouchesTrigger` overlaps a swept axis-aligned box, grown by **fixed standing
extents**, against a trigger's padded `r.absmin`/`r.absmax`. `G_TouchTriggers`
differs in four ways. The queries below do not resolve exact swept-hull contact:

| Rung | What it asks |
| --- | --- |
| `old` | the shipped test, unchanged |
| `aabb` | the same overlap with the hull native really uses - `ClientThink_real` copies `pmove.mins`/`maxs` into `r.mins`/`maxs` after Pmove, so a crouched hull is 24 units shorter |
| `swept` | the enclosing axis-aligned motion box against the trigger's **real brushes** via `trap->EntityContact`; not a true hull sweep |
| `endpoint` | what the game actually asks: brush contact at the after origin **only**, once per `ClientThink_real` after the whole Pmove |
| `processed` | endpoint contact on an entity native would dispatch - a trigger with a NULL `touch` is skipped when the toucher has none |

`ET_ITEM` never reaches `EntityContact` natively: `G_TouchTriggers` picks
`BG_PlayerTouchesItem` **instead**, on an if/else. The shipped test applies the
pickup region and then **falls through** to the generic box, so an item that
misses its own pickup bounds can still refuse on geometry native never consults.

## What was measured

Case `red_out_L1_stand`, start classes 150 and 125 - 77 of the 93 declared
weight. Four arms, three repetitions each, arms alternated, **12 of 12 cells
complete, none incomplete, 26 worlds**, ports below the host's dynamic UDP range
and bind-verified. Build `trigger-shadow-20260921-a/build`, freshly compiled;
all **1,117 source files are unchanged since it was built**.

Pooled over the six shadow-on worlds: **5,638 rollouts, 17,731,254 entity
examinations, 1,960 trigger refusals, 210 records, 0 dropped.**

| What native would do | Refusals | Share |
| --- | ---: | ---: |
| dispatch the contact - the refusal is real | 1,750 | **89.3 %** |
| not dispatch it | 210 | 10.7 % |

The 210 split by the one boundary that explains each:

| Mechanism | Count | Direction |
| --- | ---: | --- |
| `swept_not_endpoint` - enclosing box contacts, endpoint does not | 107 | unresolved: possible crossing or box false positive |
| `hull_or_pad` - all of it the item fall-through | 55 | false refusal |
| `box_not_brush` - the box overlaps, the brush does not | 48 | false refusal |

**Review correction:** the 107 cases do not prove tunnelling. Diagonal
movement can miss a brush while its enclosing axis-aligned box overlaps it.
Native checks the endpoint after Pmove; distinguishing a real crossing from a
box false positive requires an actual hull sweep. Admission stays conservative,
but the 107 cannot be excluded from possible false refusals.

The other **103 of 1,960 refusals (5.3 %)** are item fall-through or
`box_not_brush` disagreements. That subtotal is not the full recoverable set;
none of these counts establishes safe final admission or a faster trip.

## The disagreements are five entities, and the rate is stable

| Entity | Classname | Examined | Disagreed | Mechanism |
| ---: | --- | ---: | ---: | --- |
| 83 | `team_CTF_blueflag` | 358,110 | 51 | item fall-through |
| 48 | `trigger_hurt` | 457,188 | 46 | box not brush |
| 53 | `item_medpak_instant` | 457,188 | 94 | 90 unresolved swept-box, 4 fall-through |
| 82 | `team_CTF_redflag` | 457,188 | 12 | unresolved swept-box |
| 45 | `trigger_push` | 457,188 | 7 | 5 unresolved swept-box, 2 box not brush |

Per world the disagreement rate holds at **10.2-11.6 % of trigger refusals** and
3.2-4.1 % of rollouts, across both classes, all three repetitions and four
distinct start levels.

**`no_touch_anywhere` is 0.** Every trigger contacted in Kejim has a touch
function, so the dispatch gate - the rung most likely to look like free
recovery - never fires here at all.

## Phases: 86 % of the disagreements are inside the timed trip

A whole-world census includes rollouts from before the corner start. An
addendum (`protocol-phases.md`, 6 of 6 cells, 8 worlds) emitted the census three
times per world to split it. Completion times stayed at the predicted
**17,575 ms and 15,925 ms**, so the extra emission did not disturb the run.

| Phase | Refusals | Share | Disagreements | Share |
| --- | ---: | ---: | ---: | ---: |
| pre-start | 476 | 21.2 % | 29 | 13.5 % |
| **timed trip** | **1,572** | **69.9 %** | **186** | **86.5 %** |
| post-trip | 201 | 8.9 % | 0 | 0 % |

Inside the trip alone, excluding 90 unresolved swept-box cases leaves **96 of
1,572 refusals, 6.1 %**. This descriptive subtotal is not a bound on false
refusals or recoverable candidates.

## The diagnostic is decision-inert but not throughput-neutral

Both halves of this matter and they point different ways.

**Inert, where it was required to be.** `tools/test_movement_trigger_shadow.py`
compiles the real `TouchesTrigger`, the real shadow block and the real
`BG_PlayerTouchesItem` against stub geometry where a brush can be made to
disagree with its own bounding box on purpose: **25 cases pass**, covering
clear, hull-edge overlap with the origin outside, diagonal near miss,
standing and crouched hulls, hurt brushes without a touch function, item
pickup bounds, the item bypass, tunnelling, record overflow, per-trigger
census, per-rollout distinct counting, the report writer and world
restoration. Every fixture asserts that the shipped test returns the **same refusal and the same touched
entity** with the diagnostic on and off, and that the stub world is byte
identical afterwards. A deliberately wrong query - the swept rung asking the
endpoint's question - is compiled as a negative control and fails, on the
tunnelling case, as it must. Natively, **every matched start completed in
exactly the same time in both arms**: 17,575 ms at class 150 on start level
3350, 15,925 ms at class 125 on 3325.

**Not throughput-neutral, which limits what the counts mean.** At the same
start level the shadow-on worlds evaluated consistently more candidates than
shadow-off (712-736 against 457-577 at level 3350). Same-arm worlds also
disagree with each other, so this is not a clean single cause - but the ranges
do not overlap. The diagnostic examines every entity instead of returning on
the first contact, and issues two `EntityContact` queries per entity, so it
changes how much search happens per world. **Absolute counts here describe the
instrumented search, not the uninstrumented one.** The per-world *rates* are
stable within the instrumented collection; neither totals nor rates are
guaranteed for uninstrumented search.

## Decision

**Neither branch of the handoff's either/or is right as written.** There are
useful false refusals, but they are smaller and more specific than the offer
census suggested, and one of the two mechanisms is already covered by an
existing cvar.

> **Review correction and priority update.** The proposal below is parked,
> not disproven globally. The [follow-up](2026-09-21-trigger-shadow-rescue.md)
> observed 39/39 initial `arrives` conversions in the straight-search hook.
> It did not cover all 51 blue-flag census disagreements or final admission
> after chain/path checks. The historical proposal follows for context.

Of the three mechanisms, **only one is proposed for repair**:

**The item fall-through is a plain divergence from native semantics.** For
`ET_ITEM`, `G_TouchTriggers` consults `BG_PlayerTouchesItem` *instead of* a box
test; the shipped test consults both. The generic overlap on an item therefore
protects against nothing the game does. The scoped repair is to make the item
branch exclusive, exactly as native's if/else is. The safety argument is that
it removes a test native never runs and changes no other rung.

**Two things must be settled before that repair is worth qualifying**, and this
collection does not settle either:

1. 51 of the 55 are `team_CTF_blueflag` - the objective, which `StraightArrives`
   already rescues when a contact reaches it. **Whether these particular
   refusals are already recovered cannot be answered from this collection**: a
   shadow record carries its frame and call boundary but no candidate identity,
   so it cannot be joined to the `[SPIKE]` outcome of the rollout it came from.
   Settling it needs a rollout identifier in the record and a fresh collection.
   One signal points at *not* rescued: `arrives` is exactly **17 in every
   world**, across both classes, four start levels and a 3x range in candidates
   evaluated (386-1264). A count that rigid is structural, not a function of how
   many flag contacts occur. That is a hint, not a finding.
2. The other 4 are `item_medpak_instant`, `IT_HEALTH` - which
   `bot_forecastItems=1` already bypasses outright. That lever was measured and
   [refuted at both classes](2026-09-21-offer-limit-is-the-trigger-test.md).

**`box_not_brush` (48, almost all one `trigger_hurt`) is real but not
proposed.** Fixing it means calling `trap->EntityContact` inside the search,
and printing inside the search has cost 2,000 ms before. It needs its own cost
measurement first, and its own answer for moving triggers, which stay
conservatively refused.

**`swept_not_endpoint` (107) remains unresolved.** Preserve conservative
admission until exact swept-hull evidence distinguishes crossings from box
false positives. The diagnostic alone does not justify changing admission.

Nothing here shows a faster route. A recoverable refusal is an **upper bound on
what a contact-test repair could hand back to the search** - not a candidate
that is safe, executable, admitted, competitive or faster. Any continuation
experiment needs its own declared protocol and must keep unknown hazards
refused.

## Binding

| Item | Digest |
| --- | --- |
| `codemp/game/bot2_forecast.c` | `d601066cf1c2b486â€¦` |
| `codemp/game/bot2_forecast.h` | `bd73fabdc34747eeâ€¦` |
| `codemp/game/g_svcmds.c` | `999456707bfcd38aâ€¦` |
| `codemp/game/g_active.c` | `5008432fc4a1782eâ€¦` |
| `openjkded.x86.exe` | `25629be7591356f2â€¦` |
| `jampgamex86.dll` | `ae3c8912870e9df2â€¦` |

Source manifest `build/20260921T050844823203Z.sources.json` (1,117 files, all
unchanged since the build). Focused-test artifacts, including both generated
programs and the negative control's failure log, in
`.codex-research/trigger-shadow-test-20260921T052121Z-998d4e1c/`.

Every attempt world is retained, hit or miss. **Miss worlds keep their censuses
and are deliberately not pooled** into the summaries - a world that did not
complete the declared trip is evidence, not a measurement.

The diagnostic is `bot_forecastTriggerShadow`, **default 0**, emitted by the
`bot_trigger_shadow <file> [reset]` server command. It writes a file because an
rcon reply is redirected away from `server.log` and truncated. Nothing prints
from inside a search.

Historical parity evidence remains attached to `parity-fix-build-20260920-a`
and is **not** carried over to this build. No installation, production
qualification or 15-second attainment is implied.
