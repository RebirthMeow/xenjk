# Chase/base opposition at native timescale

Declared before the long campaign: Runner_v2 against two legacy Bot2 enemies,
odd enemy number forced CHASE, even forced BASE. Both runner teams at 1x for
120 game seconds, 2.5x for 300 seconds, and 5x for 600 seconds. Four isolated
worlds maximum, distinct homepaths and ports. No fixture pose/resource changes.
Use `strafe_play_smoke.py` with `--enemies 2 --game-seconds` and the declared
rate; wall watchdog 240 seconds. Build `strafe-build-chase-base-rate-respawn-20260906`.

Rates use timescale/sv_fps pairs 1/40, 2.5/100, 5/200: the engine retains a
25 ms game step. Mode 2 AI dispatch runs inside each G_RunFrame, before native
force/saber/client movement. CSV records frame duration, intervals between
command logging calls, role and rate. Legacy tactical selfkill returns without
submitting/logging a command, so that interval can be 50 ms across selfkill.
It is not an independent count of all AI invocations.

Owner clarified during collection that idle, walking and getting stuck are
the primary outcomes; captures/deaths are context. Report approximate alive
time in grounded idle (<20 units/second), grounded travel (20 to <300),
grounded fast travel (>=300) and airborne movement, with the observed alive
time as denominator. Grounded travel is an operational walking measure;
it can include grounded strafing and takeoff/landing samples.

Assess sampled alive speed below 20 units/second and spatial confinement:
at least five game seconds within 64 units of an anchor, breaking on death,
objective or capture changes. Distinguish goal arrival (within 96 units) from
confinement away from the goal. Retain episode positions, times and flag state.
This detects local stalls, not every possible large movement loop. Four-Hz
state plus event samples and demos support play review, not full-frame physics
qualification. Compare behavior across rates; random native starts are not a
paired deterministic equivalence trial.

The first 5x smoke (`strafe-rate5-smoke-20260906`) exposed an existing falling
death override resetting respawnTime to current time. Prephysics AI made the
native strict greater-than respawn gate unreachable. The correction uses
current time minus one only for this existing legacy override. Ordinary
death deadlines and Runner's native respawn behavior remain intact. Retained
90-second follow-up (`strafe-rate5-respawn-smoke-20260906`) has one Runner
capture, two Runner deaths, 95 enemy attack samples and both enemies respawning.

## Results

All six declared matches completed: 34 game minutes, plus a separate
120-game-second actual MP listen-server check at 5x. Every sampled native
frame was 25 ms; all alive actors retained the requested roles and rate.
Runner command-log intervals were 25 ms. Enemy intervals were 25 ms, with
50 ms across legacy tactical selfkill returns. Enemies respawned and resumed
movement/combat. The 2.5x worlds used devmap without fixture interventions;
the 1x and 5x worlds, including actual MP, used ordinary non-cheat map loading.

**Runner is not stall-free.** Approximate time-weighted alive measurements:

| Runner team / rate | Alive seconds observed | Idle away from goal wait | Grounded walking | Longest distant confinement |
|---|---:|---:|---:|---:|
| red / 1x | 117.35 | 16.2% | 31.5% | 7.75 s |
| blue / 1x | 116.10 | 15.5% | 32.7% | 5.00 s |
| red / 2.5x | 293.70 | 18.2% | 34.4% | 24.50 s |
| blue / 2.5x | 291.77 | 11.0% | 35.5% | 8.00 s |
| red / 5x | 577.92 | 13.1% | 31.8% | 20.25 s |
| blue / 5x | 576.35 | 16.0% | 33.3% | 22.25 s |

The two ten-minute 5x runs contain 75.5/92.25 seconds idle and 183.7/192.0
seconds grounded walking. Longest continuous grounded idle is 17.5/22.0
seconds; longest walking stretch is 11.0/7.25 seconds. Other alive time is
grounded fast or airborne movement. All times here are normal game time.

Red 2.5x also spent 45.5 seconds (15.5% of observed alive time) waiting with
the enemy flag at home while its own flag was missing. This is reported as
goal waiting, separately from the 24.5-second distant stall. Total grounded
idle in that run is 33.7%. Goal-wait classification requires carrying, a
known missing home flag and distance <=96 units to the current goal.

The actual-MP 5x check has 6.5% idle, 34.5% grounded walking, longest idle
2.0 seconds, and no >=5-second confinement episode in its declared window.
That short pass does not erase the longer dedicated-world failures.

Worst 5x episodes to reproduce next:

- Red: level 300275–320525, around (1520.287, 2718.206, 1368.125),
  fetching target entity 140, >2700 units from goal, home flag at base.
- Blue: level 511975–534225, around (2766.821, 4257.153, 1808.125),
  fetching target entity 142, >2285 units from goal, home flag at base.
- Longest across rates: red 2.5x, level 45450–69950, around
  (607.793, 1364.745, 1808.125), fetching target 143, >4296 units from goal.

Audit: `.codex-research/strafe-opposition-final-audit-20260906.json`.
Each `strafe-opposition-{red,blue}-{1x,2p5x,5x}-20260906` directory retains
source/binary provenance, settings, commands, CSV, server log and demos.
The auditor caps analysis at the declared endpoint; raw logs and original
smoke summaries include the shutdown tail. Their final native duration must
not be divided by the pre-shutdown wall duration to infer effective rate.
The earlier first-four audit is retained but includes that tail; use the
final audit for the table above. Synthetic locomotion/confinement tests pass.

Installed DLLs match this measured build in GameData and GameData/base.
Prior files are archived in `.codex-archive/20260906T0052153128129Z/` with
original paths and hashes. Close the game and archive replacements before
restoring those originals. The MP executable itself is unchanged.

Next unresolved movement gate: reduce grounded walking and idle overhead,
reproduce the distant stalls with relevant objective/player context, and
verify recovery against opposition. M3a remains open; these unpaired matches
do not establish statistical equivalence between rates or general robustness.
