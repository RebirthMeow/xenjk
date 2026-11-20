# Human play feedback: corners, jump use and facing

The second owner session reported another corner stall, excessive-looking
Force Jump and backward-looking travel. The retained pre-fix CSV is
`.codex-research/kejim-human-observations-20260906/session.csv`: 784 samples,
no captures, a 69-second sampled idle episode, and 16 airborne positive-up
samples. Four-Hz inputs do not establish exact jump duration or force cost.

## Implementation

- Localnav detects grounded lack of progress for 1.5 seconds, evaluates
  sixteen 32/64-unit steps, and walks a validated candidate before querying
  again. One candidate or revalidation is processed globally per frame,
  bounded to twelve collision/floor/hazard traces and one 64-point path
  query. Recovery is ordinary input, with no production pose/resource edits.
  `corner_step` and `corner_blocked` console events expose the decision.
- The shared velocity controller bounds facing to 60 degrees from actual
  horizontal travel above 80 speed, or intended departure below that speed.
  Existing turn-rate bounds apply while outside the cone. Backward input is
  still legal for braking; grounded launches retain forward-only input.
- `addrunner` prefers supported ordinary-hop paths (`bot_supportedHop=2`).
  Cached Force Jump routes remain fallback when supported traversal declines.
  This reduces resource use by choosing a different path/controller; it does
  not remove the levitation needed by an existing cached jump trajectory.
- Play CSV appends `yaw`, `command_yaw` and `pm_flags`. The diagnostic
  `tools/strafe_play_audit.py` reports sampled idle/facing/jump behavior.

Build: `.codex-research/strafe-build-facing-corners-20260906`.
The controller kernel check passes 4,096 deterministic, immutable-input,
legal-command cases plus a backward-facing turn-to-travel check. Existing
qualified cached-route evidence belongs to its original frozen builds;
this shared-kernel change does not automatically inherit that qualification.

## Exact-position native reproductions

The private `bot_corner_start x y z force carrying` fixture requires an unused
dedicated cheats CTF world with exactly one alive connected bot and no humans.
It collision-validates the standing pose before one declared setup change.
Carrier setup uses native item pickup. Pose, zero velocity, initial force,
inactive powers, controller reset and cleared jump/knockback are printed.
All later movement and resources use ordinary Bot2/native processing.
This recreates the logged positions, not the complete moving-defender history.

| Start | Initial force / carrier | Capture seconds | Outcome |
|---|---|---:|---|
| 1922.612, 5449.942, 1770.604 | 8 / yes | 47.350 | alive, captured |
| 1574.658, 2801.564, 1368.125 | 100 / yes | 27.275 | alive, captured |
| 1546.318, 2713.754, 1368.109 | 100 / no | 51.525 | alive, captured |

Evidence directories under `.codex-research/`:
`strafe-corner-ramp-20260906`, `strafe-corner-low-20260906`,
`strafe-corner-fetch-20260906`. Each retains setup declaration, CSV, demo and
summary. Post-intervention audit:
`kejim-human-observations-20260906/corner-post-intervention-audit.json`.
All three show no sampled force decrease or held airborne jump commands after
setup; sampled facing p95 is 48.15–48.85 degrees. Longest sampled idle is
1.5–3.25 seconds. The ramp case needs four recovery steps, the others two;
this is reactive recovery, not smooth corner-speed qualification.

## Ordinary listen-server checks

The actual installed MP executable is tested with private homepaths. Both
110-second runs captured once and remained alive, but did not finish the
requested second capture before the time limit. They are retained as
`strafe-facing-listen-red-20260906` and
`strafe-facing-listen-blue-20260906`. Longer 180-second continuation checks
are separate evidence, not replacements for those shorter results.
Both longer runs completed two captures alive, with no sampled held airborne
jump input or force decrease. Evidence:
`strafe-facing-listen-red-long-20260906` and
`strafe-facing-listen-blue-long-20260906` (summaries and sampled audits).

Installed via `tools/install_kejim_runner.ps1`; replaced assets and DLLs are
archived at `.codex-archive/20260906T0019597257797Z/`. The manifest records
source/installed hashes and original paths. Restore procedure is in the play
instructions. No permanent deletion occurred.

The new preference trades some route speed for ordinary hops and lower force
use. Moving-defender reliability, smooth turns, matched-resource speed and
continuous-play cost qualification remain open under M3a.

## Accelerated unattended outcome check

`tools/strafe_accelerated_check.py` declares 18 accelerated cases (both teams,
phases 0/3/6, force/expiry pairs 0/0, 100/0, 20/400) and two normal-speed
phase-0/full-force controls. Each runs 120 game seconds from the native
continuous fixture's declared flagstand setup. No person is needed.
Gates were recorded before execution: 4,800 frames, at least two captures,
no deaths, correct native clocks, unchanged sources, idle <=5 game seconds,
and exact paired movement/resource/command samples. All pass: 40 total
captures over 40 simulated minutes / 96,000 frames. Both pacing pairs match
all 4,800 samples on the declared fields. Separate build/source/loaded-module
verification also passes and matches the installed DLL.

Evidence: `.codex-research/strafe-accelerated-check-20260906/`. Each accelerated
run took about nine wall seconds including startup; normal controls about125.
The harness uses `fixedtime=25`, `timedemo=1` with `timescale=1`, preserving
25 ms simulation steps. In this engine, increasing timescale also scales the
server frame interval, so it is unsuitable as a drop-in movement correctness
test. At most four private native worlds ran concurrently. These are solo
two-minute outcomes, not moving-defender, long-session or performance gates.
