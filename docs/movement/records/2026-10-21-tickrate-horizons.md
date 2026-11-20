# 2026-10-21: Bot2 horizons and work budgets in milliseconds

Owner goal (2026-10-21): gameplay independent of server tickrate and client fps. This step covers Bot2's own
lookahead horizons and per-frame work budgets only. The controller, strafe tuning, `bot2_finecmd.c` and command
cadence at other tickrates are untouched; those are a later, measured decision.

Uncommitted, on branch `claude/vigilant-sanderson-aa0512` from a265c31c. Files: `ai_bot2.c/h` (two helpers),
`bot2_forecast.h` (capacity), `bot2_move.c`, `bot2_graph.c`, `bot2_wallprobe.c`.

## The change

- `Bot2_StepsFor(ms)`: the frames of the current game step that cover a duration, rounded to the nearest frame,
  at least 1 (0 for 0 ms). The step is `Bot2_GameStepMsec()`, i.e. live `sv_fps` and `timescale`.
- `Bot2_WorkPerFrame(per25)`: a budget counted per 25 ms frame, scaled to the step by `(per25*step+12)/25`,
  at least 1 while positive; 0 (off) stays 0.
- Every converted duration is a multiple of 25 ms, so at 25 ms both helpers return exactly the old counts. Budgets
  stay deterministic counts, so seeded worlds still replay.
- `BOT2_FORECAST_MAX_STEPS` was a horizon as well as an array capacity. It is now only the capacity, 800 steps:
  the longest horizon (6400 ms) at the 8 ms step of sv_fps 125. Any horizon is capped by it, so above 125 Hz a
  horizon is shorter than its stated ms.

## Converted constants

Steps at 25 ms (sv_fps 40, every test world), 33 ms (sv_fps 30, community servers) and 8 ms (sv_fps 125). The
33 and 8 ms columns give the covered time in brackets.

| Constant (where) | Old | New ms | 25 ms | 33 ms | 8 ms |
| --- | --- | --- | --- | --- | --- |
| `BOT2_FORECAST_MAX_STEPS` = `MOVE_PARITY_MAX_HORIZON` (capacity only) | 256 | - | 800 | 800 | 800 |
| `MOVE_PARITY_HORIZON_MS`: candidate rollouts (`RunCandidate`), walk references (`StraightWalkInto`), the early search's prediction cap, the `bot_moveParity` cap | 256 | 6400 | 256 | 194 (6402) | 800 (6400) |
| `MOVE_KNOCK_HORIZON_MS` (knock recovery rollouts) | 200 | 5000 | 200 | 152 (5016) | 625 (5000) |
| `MOVE_CLIMB_MS` (`climb_timeout`) | 160 | 4000 | 160 | 121 (3993) | 500 (4000) |
| `MOVE_PERMIT_HORIZON_MS` (permit rollout) | 128 | 3200 | 128 | 97 (3201) | 400 (3200) |
| `MOVE_ARRIVE_MS` (`ArrivesAt` projection after the last frame) | 4 | 100 | 4 | 3 (99) | 13 (104) |
| `PROBE_MS` (`bot2_wallprobe.c`; buffer now the forecast capacity) | 200 | 5000 | 200 | 152 (5016) | 625 (5000) |
| `MOVE_GAP_LOOKAHEAD_MS` (walk lookahead; buffer 64 -> 200) | 64 | 1600 | 64 | 48 (1584) | 200 (1600) |
| `MOVE_STRAIGHT_HOLD_MS` (run-on past a landing; was 48 x step ms) | 48 | 1200 | 48 | 36 (1188) | 150 (1200) |
| `MOVE_ONWARD_HELD_MS` (onward release, `bot_moveOnwardBound`) | 40 | 1000 | 40 | 30 (990) | 125 (1000) |
| `MOVE_TEAMMATE_MS` (guest teammate prediction; buffer 200 -> 800) | 200 | 5000 | 200 | 152 (5016) | 625 (5000) |
| `SearchPredictAt` landing slack / minimum | 12 / 8 | 300 / 200 | 12 / 8 | 9 / 6 | 38 / 25 |
| `SearchPredictAt` boundary slack and minimum | 2 | 50 | 2 | 2 (66) | 6 (48) |
| `bot_moveWalkDefer` default 6 (the value counts 25 ms frames) | 6 | 150 | 6 | 5 (165) | 19 (152) |
| `bot_moveDropDefer` default 6 (25 ms frames) | 6 | 150 | 6 | 5 (165) | 19 (152) |
| `bot_moveDeferGround` default 3 (25 ms frames) | 3 | 75 | 3 | 2 (66) | 9 (72) |

Per-frame work budgets, given per 25 ms of frame:

| Budget (where) | Per 25 ms | 25 ms | 33 ms | 8 ms |
| --- | --- | --- | --- | --- |
| `PATH_BUILD_BUDGET`: settled cells a frame (`Graph_PathTableStep`) | 4000 | 4000 | 5280 | 1280 |
| `bot_moveSearchBudget`: early-search rollouts per bot (default) | 4 | 4 | 5 | 1 |
| `bot_moveWalkPool`: deferred-search rollouts shared by all bots (default) | 12 | 12 | 16 | 4 |
| `bot_moveWalkPoolFrameEnd`: frame-end pool forecast steps (default) | 700 | 700 | 924 | 224 |
| `bot_movePlanSplit`: split-replan expansions (default 0, off; 2000 as an example) | 2000 | 2000 | 2640 | 640 |

`SearchChunkBudget` and the drop chunk already divide the work left by the frames left to the deadline, which are
computed from ms, so only their per-frame minimum (`bot_moveSearchBudget`) needed scaling. Rounding to the nearest
frame costs the search budget at 8 ms: 1 rollout a frame is 125 a second, against 160 at 25 ms.

## Left in frames, and why

- Command cadence (task scope): the climb script timings `climbJumps` / `climbRepresses` and the `climbJump+2`
  grace count commands. At 8 ms the latest jump comes 160 ms in instead of 500 ms, so they compress. This belongs to
  the later cadence decision.
- Structural minimums: `target<2` and `i-ground>=2` (a deferral needs two frames to spread work over), and
  `measure.frames<=2` (a root inside a trigger refuses at once).
- Diagnostics: the `bot_moveParity` value (counted in frames, now capped at the ms horizon), the
  `bot_moveSearchWhy` divergence track (64 frames), the guest trace (`MOVE_GUEST_TRACE_MAX` 200, default off) and
  the telemetry horizon buckets.
- Already tickrate-free: the fixed-ms simulation steps (`GRAPH_STATE_STEP_MS`, `GRAPH_PAD_STEP_MS`, the defender
  sweeps, the Golan path, `RunnerLethalAfterImpulse`) and `DEFENDER_HISTORY` (sized for 5 ms frames). Already in ms:
  `MOVE_GAP_CADENCE` (200 ms; its comment wrongly said frames and is corrected), `MOVE_KNOCK_RESEARCH`, the retry
  timers and `MOVE_KNOCK_MAX_MS`.
- Not converted: per-search candidate counts (one search, any tickrate), `bot_moveExploreBatch` (the offline
  explorer, run at 40), and the cost fixture (`bot2_movecost.c`, refuses sv_fps other than 40).

## Memory

The game DLL's `.data` (BSS) grows from 234,792,524 to 272,682,572 bytes (+36.1 MiB). That is about 10,000 added
frame slots of about 3.7 KB each (17 buffers 256 -> 800, the lookahead 64 -> 200, the wall probe 200 -> 800), plus
0.46 MB of guest prediction. Server processes sampled every 3 s (`workingset.csv`, `PeakWorkingSet64` and private
bytes):

| Arm | Peak working set | Private (commit) |
| --- | --- | --- |
| control build, ctf4 pinned, sv_fps 40 | 80 MB | 256 MB |
| treatment, ctf4 pinned and unpinned, sv_fps 40 | 80-81 MB | 292 MB |
| treatment, Kejim stand, sv_fps 40 | 87-88 MB | 304 MB |
| treatment, ctf4, sv_fps 125 | 96-100 MB | 292 MB |

At 40 the added capacity is committed but never touched. At 125 the deeper frames are touched (+16-20 MB working
set). The control Kejim stand worlds ran before sampling began.

## Verification

Builds: control `.codex-research/tickfree-control-build-20261021-a` (HEAD a265c31c, unmodified) and treatment
`.codex-research/tickfree-build-20261021-a`. Both built clean; the only warning is the existing C4142 in
bot2_defender.c. Evidence: `.codex-research/tickfree-20261021/` (scripts `stand.sh`, `ctf4.sh`, `ctf4pin.sh`,
`meter.sh`; `movediff.py` compares two worlds' `[MOVE]` lines from the corner start, with levels relative to it,
timing fields and carried-over counters dropped). The control ran against a frozen copy of HEAD's sources
(`control-src/`, all 1,102 manifest hashes matched) so that editing could go on alongside it.

**sv_fps 40, identity.** The Kejim library stand run, the 2026-09-23 production arm (`red_out_L1_stand`, seed graph,
the `explorer-k` library, classes 75/100/125/150):

| | c75 | c100 | c125 | c150 |
| --- | --- | --- | --- | --- |
| control (HEAD) | 16,350 | 16,350 | 15,925 | 18,250 |
| treatment | 16,350 | 16,350 | 15,925 | 18,250 |

Every decision line is identical from the corner start to the flag pickup in all four classes; the only differences
are lines after the pickup, where the server was stopped on wall clock. These are not the 13,625 / 14,125 / 13,400 /
14,075 ms of the 2026-09-24 wall-climb record. HEAD's defaults have moved since then: the walk, drop and window
deferrals and the level-2 guard. So the identity is against HEAD.

ctf4 climb cases (`(red|blue)_(out|ret)_L1_stand`, four worlds at once), pinned to class 100: control and treatment
both 25,700 / 25,500 / 25,500 / 25,700 ms, with one climb each and the same decisions. The one differing field,
`traces` on the first search of the two blue cases (+801), also differs between two runs of the control itself
(`control-ctf4pin-2` matches the treatment). The unpinned ctf4 arms (`*-ctf4-fps40`) are not an identity check: their
first hop depends on the pre-start state.

Not covered at 40: knock recovery, the guest teammate prediction and permits. No bench case has a defender, and no
permit fired. Their step counts are equal by construction.

**sv_fps 125 smoke.** `addrunner` sets sv_fps 40, so these arms switch with a start command (`sv_fps 125`) right
after the corner start, at timescale 1. The first try passed `--set sv_fps=125`, which the fixture overrode; it is
kept in `void-fps125-reset-to-40/`. The four ctf4 L1 stand cases:

| | blue_out | blue_ret | red_out | red_ret |
| --- | --- | --- | --- | --- |
| control (HEAD) | stall (180 s) | stall | stall | stall |
| treatment | pass 26,508 | pass 25,533 | pass 26,060 | pass 25,629 |

On HEAD at 125 Hz the frame-counted horizons cut every search short. Straight candidates fail `no_onward` (the run-on
past a landing is 48 frames = 384 ms), the onward stage is released after 41 frames (328 ms), and at a ctf4 gap
every gap candidate fails `no_onward` / `horizon`. The bot stands there for the rest of the world, and none of the four
reaches the wall. With the change each case commits its climb with a 279-281-step rollout (2.25 s at 8 ms; jump 6-8,
re-press 12). That is past both the old 256-step capacity and the old 160-frame climb timeout. No `climb_timeout`.

**Bot2 frame cost** is reported here, not judged. The treatment ran the four ctf4 cases one world at a time at
timescale 1 with `bot_moveFrameMeter 1` (`treat-meter-fps40`, `treat-meter-fps125`). The figures are the world's last
`[FRAMECOST]` / `[FRAMEWORK]` line, over every bot, from the meter reset to the pickup. This is wall clock on a busy
desktop, not a pinned timing world. All 8 worlds passed (125: 25,716 / 26,133 / 26,396 / 25,628 ms).

| Per world (range over the 4) | sv_fps 40 (25 ms) | sv_fps 125 (8 ms) |
| --- | --- | --- |
| frames metered | 999-1,039 | 3,082-3,207 |
| mean us a frame | 1,685-2,058 | 734-1,509 |
| p99 us a frame | 26,250-30,250 | 15,750-33,750 |
| max us a frame | 87,533-543,806 | 81,968-197,662 |
| frames over 25 ms | 12-16 | 12-57 |
| frames over their own step (`[FRAMESLOW]` at 25 / 8 ms) | 13-21 | 88-179 |
| forecast steps a frame: mean / p99 / max | 32-36 / 450-555 / 3,915-3,942 | 41-46 / 580-665 / 12,528 |
| est. max us (work meter) | 97,875-98,550 | 313,200 |

Per second of game time this is about twice the CPU at 125 Hz: mean x rate is about 70-80 ms/s at 40 and 90-190 ms/s
at 125. It is about four times the forecast steps: about 1,400 a second at 40 against 5,100-5,700 at 125. The
per-frame budgets keep the pooled and deferred work per second constant. Each rollout still needs about 3.1 times the
steps for the same flight, though, and the whole searches (straight, gap, climb, knock) are unbudgeted. The worst
frame at 125 is the climb search: 12,528 steps on one frame (66 candidates of up to about 281 steps, cut short by
the climb bound), against about 3,940 at 40. Whether 125 Hz servers are worth that is the owner's call. The obvious lever is
splitting the climb search over frames as the walking search already is.

## Next

The tickrate items left are the owner's to schedule:

- Command cadence at other tickrates: the climb script timings, the controller and `bot2_finecmd.c`.
- Whether 30 Hz servers should keep these 25 ms-nominal values or be tuned to their own feel (the reference-value
  choice in the owner's goal).
- Game logic per server frame: saber, lightning, triggers.
