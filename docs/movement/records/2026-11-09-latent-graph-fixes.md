# 2026-11-09: latent fixes BP-3, BP-4, BP-10, BP-11 and BA-3/O19

Branch `claude/bot2-review-next` (cut from `renderer/pbr-materials` 161d1423, with `claude/bot2-replan-budget-be5722`
and `claude/bot2-exec-deep-bab45d` merged in), commit bb4c06dd. Review items in
[docs/review/2026-11-09-bot2.md](../../review/2026-11-09-bot2.md); lane report
[plan.md](../../review/2026-11-09/bot2/plan.md) for BP-*, [ai.md](../../review/2026-11-09/bot2/ai.md) for BA-3.
Each fix is latent on the prepared maps: none should change a laps world, so they are checked by replay, not by a
laps pair (the owner's order for this work).

## The changes

**BP-3, the graph cache's file policy** (`bot2_graph.c`: `CachePath`, `LoadCache`, `SaveEdges`, `PostLift`).
`Load` refuses a cache on its version, grid, physics, size or any malformed line, and every refusal used to fall
through to a build saved over the file it could not read: one server started with another `g_speed` replaced a
94,711-edge seed with an unseeded build, silently. Now:
- the physics binding is in the file name when it is not the default (`<map>_<checksum>_g<gravity>_s<speed>.txt`);
  the default (800, 250) keeps the plain name every seed, tool and staged world uses, and the `.guide` goal list
  keeps the plain name for every binding (goal positions do not depend on physics);
- a cache that exists and does not load is never written over: the load tries `<name>.rebuilt.txt` next, and the
  build is saved there; with both unreadable nothing is saved (`[GRAPH] cache_unreadable file=... reason=...
  kept=1`, `cache_write_skipped`). Reasons: `version`, `physics`, `header`, `size`, `malloc`, `malformed`,
  `edge_invalid`, `pre_lift`;
- the pre-lift rebuild (a cache with no mover cells on a map with lift calls) goes to the rebuilt slot too, and the
  old file stays;
- a save writes `<file>.tmp` in full, then the file, then empties the `.tmp` (the game's file API has no rename, so
  the swap is two writes); a load that finds the file unreadable and the `.tmp` whole takes the `.tmp` (a save that
  was interrupted). A world's `bot2-graph/` therefore holds an empty `<name>.txt.tmp` after its first save;
  `world_retention.share_graphs` globs `*.txt` and does not see it.
- `move_graph rebuild` (an operator's command) still writes the current target.

**BP-4, guidance arcs** (`GuideArc`, `GuideCompactArcs`, `GuideSync`). Merging only unlinked repeated arcs, so the
array kept them (Kejim's load: 852,114 of 1,048,576 used, 548,866 of them merged), every learned edge appended 7.9
more, and a full array stopped guidance for the rest of the map with nothing printed. Now every merge is followed by
a pack (each cell's list keeps its order, so every settle relaxes the same arcs in the same order), a full array
merges and packs before it gives up, an overflow prints `[GRAPH] guide_overflow` once and makes the next
`GuideSync` rebuild, and a rebuild that overflows again stops guidance until the graph changes (no rebuild loop).
The pack's copy is a temporary allocation (16 bytes an arc, only at a rebuild or a full array). The logs print the
live arcs as before (`arcs=303248 merged=548866` on Kejim's load).

**BP-10, loaded edges** (`EdgeRecordValid` in `Load`; `tools/movement_graph_merge.py`). A record needs a known type
(1-7), a cost of at least 1 ms, finite floats and, for a lift, an inline model in 1..511; otherwise the file is
refused as `edge_invalid` (and, by BP-3, kept). The merge tool refuses caches of different versions (the version is
now in its binding) and a folded result over the game's 131,072-edge cap. Every prepared seed and every staged
world cache on this machine passes: 12 seeds in `.codex-research/mapprep-20260927-a/seeds/` and 40 world caches, 0
bad edges (`.claude/worktrees/bot2-review-next/.codex-research/latent/seedcheck.py`; Kejim costs 50-3,787 ms).

**BP-11, routes longer than 1,024 cells** (`PathSearch`, `StateResult`, `PathFromTable`). All three returned 0,
the answer for unreachable. Now they return the first 1,024 cells with the whole route's cost, the bot replans on
the way, and `[GRAPH] path_truncated` prints once a map. Every caller passes `GRAPH_MAX_PATH`. Kejim's longest
cross-map routes are 133-166 cells, so no prepared map reaches it.

**BA-3 and O19, `addrunner`'s saved settings** (`bot2_svcmds.c`, `g_main.c`, `ai_main.c`). `bot_telemetry` is
registered without `CVAR_ARCHIVE` (O19). `addrunner` keeps the values of the archived settings it sets before the
first `addrunner` of a game (`sv_autoDemo`, `sv_autoDemoBots`, `sv_autoDemoMaxMaps`, `pmove_float`, `pmove_msec`,
`g_forceRegenTime`, `g_forcePowerDisable`, `g_jediVmerc`) and puts them back at the game's final shutdown (a map
change or a quit; not a `map_restart`), printing `[BOT2 PLAY] addrunner settings restored`. The play log is per
game already, so a later map needs `addrunner` again. A profile whose config already holds `seta bot_telemetry 1`
keeps it archived (a `seta` cvar stays archived); remove that line from that config by hand once.

## Expected effect (written before measuring)

None in laps worlds: the seeds load, no route nears 1,024 cells, the arc array does not fill, and the restore runs
after the last decision. The worlds must replay the guide campaign's control exactly (game log and every decision
line). The BP-3 path is checked on its own: one bench world on a Kejim seed truncated at 10,000,000 bytes (the
header says 94,711 edges, so the load fails as `malformed`) must print `cache_unreadable ... reason=malformed
kept=1`, leave the staged file byte for byte, and write a `.rebuilt.txt`.

## Measurement

Build `.codex-research/review-next-build-20261109-c` (bb4c06dd), evidence `.codex-research/latent-20261109-a/`
(`runlatent.py`, `run.sh`, `chain.log`, `kejim_truncated_seed.txt`, `worlds/`, `bench-truncated/`). The bench world's
sources are bound to the frozen worktree `.codex-research/wt-review-next-c` (bb4c06dd), since the branch kept moving.

## Measured (2026-11-09)

- **Replay.** The defaults on build -c replay the guide campaign's control on Kejim and Dantooine seed 3: game log
  487 and 477 lines, decision lines 148,173 and 151,020, all identical (`identity.txt`). Each world's
  `bot2-graph/` holds the saved cache and an empty `<name>.txt.tmp` (Kejim 14,471,620 bytes, Dantooine 1,069,406).
- **BP-3's unreadable path.** The bench world (`red_out_L1_stand`, class 150, passed in 21,375 ms) printed
  `[GRAPH] cache_unreadable file=bot2-graph/mp_ctf_kejim_-1301924053.txt reason=malformed kept=1`, built unseeded
  (`cached=0`, 112 edges by the end), and saved it to `mp_ctf_kejim_-1301924053.rebuilt.txt` (814,061 bytes) with an
  empty `.rebuilt.txt.tmp`. The staged truncated seed is unchanged: 10,000,000 bytes, SHA-256 4b0aff7d000243da...
  before and after.
- Not exercised natively: the `.tmp` recovery, the physics name, BP-4's overflow, BP-11's truncation (none occurs
  on a prepared map). The deferred-save test (`tools/test_movement_graph_save_defer.py`) now models the two-file
  save and passes; the other graph and executor tests that extract this code pass (18 tests).

All five are latent fixes with no world change; nothing to decide.
