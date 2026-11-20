# Fork review (started 2026-10-07)

Owner goal: a clean, bug-free and efficient code base, reviewed from the OpenJK fork point to now, before adding
features. Fork point: merge-base `407a56fb` (OpenJK, 2025-12-07); 344 fork commits at the start.

## Owner decisions (2026-10-07)

- **Dead code: remove what lost.** Off-by-default experiment switches whose records closed them, and legacy code
  nothing reaches (the pre-rewrite runner stack among it). The records stay.
- **Review first**, then merge the newer OpenJK commits (13 at the start: libpng 1.6.53, a dedicated-server jk2 model
  crash fix, `fx_rain` defaults, persistent `cl_reconnectArgs`, icons, CI).
- **Renderer:** rend2's look with Vulkan's performance; vanilla is outclassed and rend2 hitches. Target: a Vulkan
  renderer extended toward rend2's features. See [renderer-architecture.md](../renderer-architecture.md).
- **Mod features: survey first** (candidates with value and cost), owner picks.

## Method

Each finding is verified before it is fixed (extracted-C reproduction, build or test), ranked P0-P3, and fixed in
batches with the cheapest check that shows its risk (no routine full gates). Removals cite the record that closed the
switch. Findings and their outcomes go in [FINDINGS.md](FINDINGS.md).

## Areas

| # | Area | Size at start (added lines) | Status |
| --- | --- | --- | --- |
| 1 | Engine, renderers, client, server, qcommon, build files | ~800 in 54 upstream files, client ~650 | reviewed; E1-E7 fixed (8c4a1aad), E8 with the renderer work |
| 2 | cgame | ~5,300 in 17 files | reviewed; C1-C4, C6, C8, C11 fixed (5b94b67c) |
| 3 | Lost experiments and legacy code (inventory, then removal) | - | removed, verified identical (see FINDINGS) |
| 4 | Bot2 graph and planner (`bot2_graph.c`, `bot2_graph.h`) | ~3,450 | reviewed; G1-G3 fixed (latent, replays the control) |
| 5 | Bot2 move, forecast, pmove (`bot2_move.c`, `bot2_forecast.c`, `ai_bot2_pmove.c`) | ~10,700 | reviewed; M1-M8 fixed, M9-M15 open (FINDINGS) |
| 6 | Defenders and aim (`bot2_defender.c`, `bot2_aim.c`, `bot2_threat.c`) | ~3,900 | reviewed; D1-D6 fixed, D7-D11 open |
| 7 | Objective, integration, older controllers (`ai_bot2*.c`) | ~10,000 (5,900 after 3) | reviewed; O1-O14 fixed; the classic Bot2 removed (owner), replays the control |
| 8 | Fixtures, benches, meters (`bot2_*bench.c`, `bot2_corner.c`, `bot2_movecost.c`, ...) | ~4,500 (2,800 + 420 kernels) | reviewed; F1-F17 fixed |
| 9 | Vendored code: Detour, theora, vorbis | ~151,000 libs | reviewed; V1, V2, V4 fixed; trimmed to what builds (owner) |
| 10 | Python tools (321 files) | ~67,000 | reviewed; T1-T10 fixed, T11-T15 open; tools of removed code removed (owner) |
| 11 | Upstream merge | 13 commits | merged c6320430 (all 13; libpng 1.6.16 -> 1.6.53, the dedicated jk2-model crash fix); Dantooine laps 1-4 replay exactly |

Areas 4-8 shrink once area 3's removals land, so legacy code is removed before it is reviewed.

## Next session

The review is complete (areas 1-11). Done 2026-10-08: area 11, the upstream merge (c6320430, clean; game, server, client and both renderers build; `.codex-research/upstream-merge-20261008-a/`). Done 2026-10-08: areas 9 and 10 (the shed never takes a person's demos; 47 sessions' demos were already gone). Done 2026-10-08: area 8 (F1-F10 fixed: the cost fixture starts again, fixtures own their bots (O16), the mover rest memo is per real frame; laps worlds replay but Kejim 11, `.codex-research/review8-20261008-a/`). The classic Bot2 was removed after area 7 (owner); `bot_test_runner.py` and `defense_laps.py --vs-legacy` go with area 10. Done 2026-10-08: area 7 (O1-O14 fixed; laps captures Dantooine -0.06 +- 0.55, Kejim -0.03 +- 0.40, `.codex-research/review7-20261008-a/`); area 6 (D1-D6 fixed; laps captures
Dantooine -0.00 +- 0.47, Kejim +0.59 +- 0.35, `.codex-research/review6-20261008-a/`). Open from area 5 for the owner: M9 (a climb ahead takes the approach's
frames), M11 (keep-out bodies in crowds). Done 2026-10-08: area 5 (M1-M8 fixed; laps captures Dantooine +1.11 +- 0.37,
Kejim -0.49 +- 0.36 undecided, `.codex-research/review5-20261008-a/`); area 4 (no live defect; three latent ones
fixed, Dantooine laps seeds 1-4 replay the defaults control, `.codex-research/review4-20261008-a/`). Done 2026-10-07:
areas 1-3, production settings as the code defaults and hop searches without sv_cheats (f880bb67). Client tests launch from a private folder
(`.codex-research/client-test-20261007-a/bin` with SDL2/OpenAL copies, its own homepath) with `s_volume 0` and
`s_musicvolume 0` (owner), and capture the game window with PrintWindow, never the screen.
