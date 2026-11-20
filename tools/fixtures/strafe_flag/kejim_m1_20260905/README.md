# Accepted Kejim movement benchmark

Frozen 2026-09-05: 24 native spawn poses and both flagstand-to-flagstand
starts. `catalog.txt` is the single runtime asset. `guides/` contains spatial
proposals; `planned/` contains native command captures used for same-endpoint
references. These are development references, not optimal TAS or human demos.

The accepted controller completed 26 planned pickups and all 208 runtime
phase trials. `accepted-scorecard.json` records each start's reference, nominal
and worst-phase times. `provenance.json` preserves source paths and byte hashes;
those historical paths are not needed to run this package. Keep these files
frozen and create a separately named package when changing the benchmark.

Physics: XenJK, `mp/ctf_kejim` checksum -1301924053, 40 Hz, fixed 8 ms pmove,
speed 250, gravity 800, `g_forceRegenTime=200`, movement layers enabled.
The fixture starts with 100 health/force and level-three Speed/Levitation.
It verifies native collision, command clocks, settings and actual enemy-flag
pickup. It does not establish historical base_enhanced regeneration behavior.

Run from the repository root with your own local Jedi Academy game assets.
Use a fresh build/output path each time; existing evidence is retained.

```powershell
python tools/strafe_fixture_build.py --out .codex-research/my-strafe-build
python tools/strafe_qualification_batch.py --build .codex-research/my-strafe-build --assets "game_directory/Jedi Academy/GameData" --inventory tools/fixtures/strafe_flag/kejim_m1_20260905/inventory.json --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --cases tools/fixtures/strafe_flag/kejim_m1_20260905/cases.json --out .codex-research/my-kejim-qualification --workers 4 --accelerated
```

`--accelerated` is for native travel/outcome measurement. For cost, stop other
native trials and builds, then run the normally paced actual-client fixture:

```powershell
python tools/strafe_cost_runner.py --build .codex-research/my-strafe-build --assets "game_directory/Jedi Academy/GameData" --catalog tools/fixtures/strafe_flag/kejim_m1_20260905/catalog.txt --out .codex-research/my-kejim-cost-eight --bots 8
python tools/strafe_cost_report.py --run .codex-research/my-kejim-cost-eight --out .codex-research/my-kejim-cost-eight-report
```

The runners stage private binaries/configuration in isolated localhost server
homes; they do not install this catalog into a live server. M1 coverage does
not qualify disturbed recovery, return/capture behavior or continuous matches.
See `docs/STRAFE_MOVEMENT_PROJECT_PLAN.md` for the separate M2/M3/M3a gates.
