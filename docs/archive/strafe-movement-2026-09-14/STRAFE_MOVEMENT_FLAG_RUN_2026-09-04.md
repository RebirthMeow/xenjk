# Spawn-to-enemy-flag movement benchmark

The user's current step 3 is a complete strafe traversal from a spawn to the
enemy flag stand, with travel time comparable to a pre-planned/TAS route.
This is a narrower milestone than a complete intermediate-human offense bot.

The experiment uses a real red spawn on `mp/ctf_kejim`, native XenJK movement,
and the **actual blue flag pickup** as its endpoint. Step 3 passes on this
map/spawn: **6.70 seconds for live feedback versus 6.60 seconds for frozen
native commands**, 1.5% slower. All 12 feedback sensitivity cases pick up.

## What was built

`codemp/game/bot2_flagrun.c` owns an opt-in, isolated dedicated-server run.
Stock integration consists of a server command and one client execution hook;
the existing Bot2 behavior is not automatically replaced. `bot_flagrun 0`
requires a fresh dedicated cheat-enabled process and one bot. The Python
runner stages its own executable, DLL, symbols, configuration and writable
home directory, and shuts down only its own server. Installed game assets
are read-only.

The controller uses an existing TAS route as a spatial guide, then chooses
legal movement commands from the current player state at 40 Hz. It projects
onto nearby guide segments, tracks their direction and speed, and searches
72 wish directions with eight movement-key combinations using a cheap
horizontal acceleration estimate. Camera turning is capped at 360 degrees
per second. This cap is a mechanical constraint, not a human-skill calibration.

Native floor traces at the guide points supply possible landing heights.
Jump release uses the next takeoff hint and estimated remaining airborne
travel; actual ground contact starts the next maneuver. Ordinary forward
input at launch avoids the native saber backflip and wall-acrobatic branches.
The final approach aims at the live flag entity rather than the imported
route's broad goal region. There are no embedded Kejim coordinates in the
controller. Route-window sizes and travel estimates remain heuristic and
are validated on this route only.

This deliberately uses a simpler controller than the earlier eight-candidate
native forecasting experiment. That experiment showed continuation scoring
could improve two-platform traversal, but its 44–77 ms decision time did not
meet the provisional budget. This full-route controller does not run that
search online and does not establish full-world simulator parity. Native
server gameplay executes every submitted command, including force powers,
items, collisions, damage and the flag pickup.

## Route and comparison contract

The seed is the existing, read-only file:

`<UnrealJKA>/JKA/Content/GlobalRoutes/ctf_kejim_spawn_t1_i2_goal1.jkafrugal.csv`

It contains 49 control segments and reports 6.207997 seconds in the Unreal
TAS simulator. Import converts world Y and view yaw signs; movement key signs
are preserved. Direct clock-based replay of the imported tape failed in
native XenJK. Its finish region is also broader than an actual flag pickup.
**The 6.208-second value is context, not a same-endpoint native baseline.**

The fair native pre-planned baseline is the complete command sequence from
an earlier successful development feedback run, frozen before the comparison:

`.codex-research/strafe-flag-frugal-feedback40-aligned-20260904/home/base/flag_live.csv`

Byte-identical copies of both references are included under
`tools/fixtures/strafe_flag/`, with provenance, for self-contained reproduction.

Replay preserves the exact packed view angles and command timing. It performs
no feedback correction. It is a valid pre-planned execution baseline, but is
not an independently optimized or globally optimal TAS result. Nominal live
feedback must finish within 1.20 times that baseline's travel time. Failed
episodes are included separately rather than averaged away.

Both arms use:

- Map checksum `-1301924053`; settled red spawn origin
  `(-64, 640, 1808.12488)`, rest velocity, yaw 67.5 degrees. The map's spawn
  entity is at `(-64, 640, 1832)`, 23.875 units above the settled root.
- `sv_fps=40`, `pmove_fixed=1`, `pmove_msec=8`, `pmove_float=1`,
  `g_speed=250`, `g_gravity=800`, `g_forceRegenTime=200`.
- Saber ready, rank-3 Speed and Levitation, initial force 100. The first
  timed command activates Speed through normal gameplay, spending 50 force.
- The real flag drops to `(2464, 5312, 1900.125)`. Success requires the
  native enemy-flag powerup and positive health, not proximity alone.

The force regeneration setting is an explicit XenJK experiment setting.
It is not an inference about the historical base_enhanced servers or their
custom regeneration values. No human demo has been promoted to a clean
movement label in this experiment.

Movement time starts after a short stationary setup interval. The root is
placed at the settled real spawn once, before timing; there are no later
teleports, velocity resets or force refills. Setup wait and setup-to-pickup
time are reported separately. The wait aligns a 200 ms common command/physics
phase. Additional runs sweep every 25 ms offset within that period.

## Evidence and robustness scope

The 17-case matrix compares nominal and four impulse cases in both arms, then
tests the feedback controller at seven additional timing phases. Each impulse
adds exactly 50 units/s along one signed world X/Y axis at 2.5 seconds. These
are explicitly injected disturbance tests, separate from clean solo episodes;
they are not claims about full rocket explosions or body-blocking recovery.
The runner rejects other clients, NPCs and missiles.

Development failures are preserved under `.codex-research/strafe-flag-*`.
They exposed incorrect reuse of backward-adjusted speed, native saber
backflips, phase-sensitive platform clearance, missed takeoff windows after
late landings, and overlong jumps after adding uniform clearance. The final
approach uses native-supported landing hints instead of a uniform taller arc.
The phase/impulse matrix was used during development; it is a regression and
bounded sensitivity sweep, not an unseen test set or statistical population
estimate.

## Final clean-build results

The final matrix is `.codex-research/strafe-flag-final-matrix-20260904`;
the independent passed audit and route plot are in
`.codex-research/strafe-flag-final-audit-20260904`. The latter also preserves
the source version that produced the result, before live-match integration.
Every runtime retains its exact executable, DLL, symbols, inputs, native
CSV, settings readback, checksum, loaded-module path and logs.

| Case | Frozen commands | Live feedback |
| --- | ---: | ---: |
| Nominal | 6.600 s | 6.700 s |
| X velocity −50 at 2.5 s | Death, 9.900 s | 6.725 s |
| X velocity +50 at 2.5 s | 6.700 s | 6.675 s |
| Y velocity −50 at 2.5 s | Timeout, 20 s | 6.700 s |
| Y velocity +50 at 2.5 s | Timeout, 20 s | 6.650 s |

The additional feedback timing phases finish in 6.625, 6.575, 6.700, 6.550,
6.775, 7.825 and 6.700 seconds. The slowest needs a seventh jump; all others
report six. These are specific deterministic probes, not 12 independent
random trials. Nominal setup wait is 0.300 s for both arms, making
setup-to-pickup 6.900 s planned and 7.000 s feedback. Setup waits across the
sweep range from 0.300 to 0.550 s and are included in each report.

The controller costs 45–107 microseconds per decision on average per feedback
episode; p95 ranges from 46 to 247 microseconds. The largest individual
measurement is 2.246 ms. Costs include guide selection, action generation
and occasional landing diagnostics, but exclude native gameplay execution,
per-frame CSV writing and the one-time 49 floor probes. These are single-bot
wall-clock measurements, not a many-bot server deadline guarantee.

A fresh Win32 RelWithDebInfo engine/game build succeeds. All 36 relevant
Python tests pass. The original 30 Hz/fixed-integration fixture passes 122
live/predicted movement, command, policy and contact checkpoints with zero
movement-contract mismatches. Its two previously documented excluded fields
still differ; this does not establish general full-server-state parity.
The regression is `.codex-research/strafe-flag-final-regression-20260904`.
The build has existing warnings in unrelated Bot2/wallrun and stock code;
none are reported for the new flag-run source. All test servers exited.

## Reproduction

Run from the repository root; every output name must be fresh.

```powershell
python -B tools/strafe_fixture_build.py --out .codex-research/flag-build-NEW
python -B tools/strafe_flag_matrix.py --build .codex-research/flag-build-NEW --assets 'game_directory/Jedi Academy/GameData' --tape tools/fixtures/strafe_flag/kejim_red_i2_guide.csv --native-commands tools/fixtures/strafe_flag/kejim_red_i2_planned.csv --out .codex-research/flag-matrix-NEW --port 29650
python -B tools/strafe_flag_report.py --matrix .codex-research/flag-matrix-NEW --out .codex-research/flag-audit-NEW
python -B -m unittest discover -s tools -p 'test_strafe*.py'
python -B -m unittest discover -s tools -p 'test_bot_test_runner.py'
```

The report independently reads native CSVs, checks artifact/source hashes,
compares the nominal planned replay against the frozen movement sequence,
checks the shared initial/physics contract and feedback yaw cap, and requires
actual pickups in all 12 feedback cases. It generates a route/height plot.

## Connection to the offense bot

This provides a measured movement component that can execute a supplied
offense route at close to the pre-planned run's time. Next integration should
give Bot2 objective logic ownership of route selection and movement handoff,
then validate additional spawns/routes and interruption behavior. Automatic
route discovery, arbitrary combat recovery, flag return, intermediate-human
calibration, tactical force use, survival and shooting remain separate work.
The completed route must not be mistaken for those capabilities.

The owner subsequently redirected work toward live-match cost. The first
shared-kernel and normal-dispatch integration, with its validation and known
recovery bottleneck, is recorded in
[the live-runtime follow-up](STRAFE_MOVEMENT_LIVE_RUNTIME_2026-09-04.md).
