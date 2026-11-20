# Deferral behaviour on retained worlds (2026-10-05)

Owner's first item for the session: the walk and drop deferrals were judged by captures and leg times only; measure
their behaviour directly (freezes, standing and walking instead of hopping, time at drop ledges), off against on, on
the worlds already run. No new native runs. Scripts and outputs: `.codex-research/ledge-20261005-a/` (`ledge2.py`,
`ground.py`, `solo.py`; `drop-dan.txt`, `walk.txt`, `walk-samebuild.txt`, `ground.txt`, `solo.txt`). `ledge.py` and
`drop.txt` are a first pass whose ledge zone (within 160 u of any of 226 drop commit sites) covered most of Dantooine
and is not used.

Measures (4v4 play samples, 250 ms a client; roles pooled over both teams; 16 worlds paired by seed, diffs mean +- se):

- **still / walk / air:** share of alive time grounded under 60 u/s, grounded 60-300 u/s, airborne.
- **drop episode:** a client's drop lines for one edge (`drop_plan=1` or `kind=edge reason=deferred_*`) with gaps of at
  most 1.5 s; its standing time is the ledge pause, its deaths are those inside it or within 1 s after it.
- **frozen:** every bot within 50 u of where it was 10 s earlier (`b3-20261002-a/stalls/frozen.py`'s test).

## Drop deferral: no runner ledge pause; one defender ledge stall, fixed

Dantooine, against `drop-20261005-b/control` (Kejim's drop arms are identical worlds and have no drop edges):

| Runner, per world | control | before the fix (`drop-20261005-b/defer`) | with the fix (`dropfix-20261005-a/defer`) |
|---|---|---|---|
| standing in drop episodes | 5.0 s | +2.1 +- 1.7 | +1.2 +- 1.3 |
| deaths in drop episodes | 2.5 | -0.75 +- 0.72 | -0.19 +- 1.07 |
| still / walk / air % | 13.3 / 10.8 / 50.5 | +2.1 +- 2.2 / -0.0 / -0.4 | +1.0 +- 1.5 / +0.2 / +0.3 |
| deaths, all causes (samples) | 41.8 | 42.0 | 41.6 |

- **The runner's "ledge pause under fire" is not there.** Runners stand at drop edges 1-2 s a world longer, within
  spread, and are not killed there more often. Their total deaths do not change: the extra weapon deaths of the
  pre-fix arm (+2.8) match its fewer falls (-2.0), so runners that no longer fall live to be shot.
- **The real ledge wait was a defender.** Before the fix, in `dan-6` Defender2 (client 5) stood at speed 0 beside drop
  edge 138 from 152 s to 579 s of game time and logged 983 `deferred_drift_none` re-deferrals. That one world gives the
  arm's defender standing-in-episode mean of 38 s against 3 s. With the fix no client in any world has more than 100
  standing drop lines. Defenders still stand +4.5 +- 1.2 s a world longer in drop episodes (campers +5.3 +- 2.5): real
  but small.
- The deferral removes failing drop searches: chaser drop-episode time -66 s a world (+-12) and chaser deaths in drop
  episodes -11.9 +- 3.9.
- No world in any arm has a frozen stretch.

## Walk deferral: every role hops less

Same build (`walk-20261004-c`, control against K = 6 pool 12):

| air % (control), change | Dantooine | Kejim |
|---|---|---|
| Runner | 55.9, -4.6 +- 0.6 | 58.9, -4.7 +- 0.8 |
| Defender | 25.0, -4.0 +- 0.7 | 27.6, -2.6 +- 0.5 |
| Chaser | 39.1, -6.0 +- 0.7 | 42.0, -6.5 +- 0.4 |
| Camper | 41.8, -6.6 +- 0.9 | 45.1, -4.2 +- 0.6 |

The default (with the rescue, `rescue-20261004-a/defer` against the same control) agrees: runner -5.4 +- 1.0
(Dantooine), -3.4 +- 0.5 (Kejim). The lost air time goes to the ground, mostly moving: Dantooine runner walking +1.0
+- 0.2 points, still +0.4 (within spread), the rest grounded over 300 u/s. Standing still does not change beyond spread
for any role except Kejim chasers (+2.9 +- 0.7).

The runner's ground stretches between hops get longer (`ground.txt`; 250 ms samples, so taps are not resolved):

| Runner | hops a world | mean stretch (samples) | 1 sample | 2 | 3 | 4-7 | 8+ |
|---|---|---|---|---|---|---|---|
| Dantooine control | 545 | 2.94 | 30.4% | 27.2% | 15.4% | 17.9% | 9.0% |
| Dantooine, walk deferral (three arms: walk defer, rescue, dropfix) | 511-520 | 3.35-3.41 | 23-24% | 23% | 16-17% | 24-25% | 12% |
| Kejim control | 574 | 2.78 | 36.6% | 24.1% | 14.6% | 17.3% | 7.4% |
| Kejim, walk deferral (same three arms) | 553-567 | 3.04-3.10 | 32-33% | 21-22% | 15.4% | 21% | 9.5-10% |

About 115 ms more ground per stretch on Dantooine and 65 ms on Kejim, with about 5% fewer hops. The deferred search
delays a walking hop by K frames, and a refused take waits again, so landings that chained into a hop now run on the
ground first. Captures did not show this (team captures within spread).

## Solo bench (drop deferral)

`dropsolo-20261005-a`, 36 legs a class, each leg cut at its objective (pickup outbound, capture on return; the bench's
linger tail is excluded, where one c150 world stands 12 s after its pickup in both legs it serves). Walk deferral is on
in both arms.

| per leg, on - off | c50 | c150 |
|---|---|---|
| still | -0.02 +- 0.02 s | -0.01 +- 0.01 s |
| walk | +0.17 +- 0.08 s | +0.06 +- 0.05 s |
| air | +0.14 +- 0.06 s | -0.16 +- 0.10 s |
| longest stand in a leg | 0.5 s (off 0.5) | 0.5 s (off 0.5) |

Alone the runner never waits at a ledge; the deferral's cost is a little walking.

## Conclusions

1. Drop deferral (default, with the fix): no ledge pause for runners, no freezes; the one ledge stall found (defender,
   pre-fix) is gone. Small extra defender and camper standing at drop edges remains.
2. Walk deferral (default): a behaviour change the capture measures missed. Every role is airborne 2.6-6.6 points less of
   its alive time, and the runner's ground time between hops grows 9-16%. This is against the owner's style
   preference for chained jumps (smooth weave, chained hops).

## Where the walk deferral loses hops (owner: option 1, 2026-10-05)

Scripts `stretch.py` and `cadence.py`, outputs `stretch-dan.txt`, `stretch-kej.txt`, `cadence-dan.txt`,
`cadence-kej.txt` (same directory). Every ground stretch, landing to next launch, is taken at frame resolution from the
`[MOVE]` land and launch events and labelled by what started the launch: the chain's next hop, or the last committed
plan in the stretch (straight search at the landing, straight search on the walking cadence, gap, climb, edge, state).
Arms: `walk-20261004-c/control`, `walk-20261004-c/defer` (same build), `rescue-20261004-a/defer` (the default).

**The lost hops are the walking search's.** Hops started by the landing search are unchanged (launched about 50-95 ms
after the landing in every arm). Hops started by the cadence search fall, and the ground before each grows:

| per world, cadence-started hops (mean ground before) | control | walk deferral | default (rescue) |
|---|---|---|---|
| Dantooine runner | 145 (1.19 s) | 115 (1.57 s) | 120 (1.60 s) |
| Dantooine chaser | 149 (1.77 s) | 88 (1.91 s) | 116 (1.91 s) |
| Dantooine camper | 146 (1.61 s) | 89 (1.89 s) | 113 (1.89 s) |
| Kejim runner | 190 (1.09 s) | 140 (1.26 s) | 143 (1.32 s) |
| Kejim chaser | 201 (1.57 s) | 114 (1.78 s) | 153 (1.76 s) |

Chained hops fall with them (Dantooine runner 156 -> 133 a world): fewer sequences start.

**Why: the cadence restarts at the take.** A walking search falls due 200 ms (`MOVE_GAP_CADENCE`) after the last one,
which `StraightSearch` stamps on entry (`m->straight.time=level.time`). Deferred, the search due at t is predicted for
t+150 (K = 6) and taken at t+150 through `StraightSearch`, so the stamp is t+150 and the next falls due at t+350. Each
walking decision is 150 ms late and they come 60% less often. Measured interval between a walking bot's consecutive
straight-search decisions: control mostly 250 ms (53% Dantooine, 40% Kejim); deferral mostly 400 ms (34-37%, 22-23%).
Walking decisions a world: Dantooine 2,151 -> 970 (deferral) / 1,347 (rescue); Kejim 2,997 -> 1,578 / 2,075.

Tallies a Dantooine world (default): 7,766 deferrals started, 4,953 silent exits from the predicted state (no straight
ahead and the like, which restart the cadence as a live exit does), 1,488 taken, 400 late, 439 prediction failed
(searched whole), 151 refused, rescue 299 no-hop / 153 tried / 122 committed, 1,685 chunk frames starved of pool. The
rescue's gain is the refusals (931 -> 151): it recovers part of the aiming roles' hops, not the cadence.

## Option 1 built: cadence and retry from the deferral's start (owner, 2026-10-05)

`bot_moveWalkCadenceStart` (default 0; 0 replays the current default). On a deferred walking search's take, the
cadence stamp (`m->straight.time`) and a no-hop result's retry (`m->straight.searchTime`, `MOVE_GAP_RETRY` 250 ms) are
timed from the frame the deferral fell due on; a deferral's silent exit restarts the cadence from that frame too.
The first build (`cadence-build-20261005-a`, `.codex-research/cadence-20261005-a/`) moved only the cadence stamp;
walking decisions stayed at 400 ms because the retry, not the 200 ms cadence, binds after a no-hop result. It was
stopped after 4 worlds a map (partial worlds kept, noted in its `run.log`). Measured build: `cadence-build-20261005-b`,
`.codex-research/cadence-20261005-b/` (`run.sh`, `laps-compare.txt`, `share.txt`, `stretch.txt`, `cadence.txt`,
`decisions.py`/`decisions.txt`, `kej-arms.txt`, `solo.py`/`solo.txt`, `timing.py`/`timing.txt`).

- **Identity:** off, Dantooine seed 1 replays `dropfix-20261005-a/defer/dan-1` (game log identical but the version
  line; play samples identical to the world's end).
- **Solo bench** (on, against the default's arms): 72/72 pass; 68 legs identical, four c50 respawn-4 legs (L2/L3 out
  and return, one shared outcome) +275 ms; total +1.1 s (+0.1%).
- **The rhythm is restored:** the most common interval between a walking bot's decisions is 250 ms again (Dantooine
  33%, 400 ms 34% -> 11%); walking decisions a world 1,230 -> 1,596 (control 2,151), Kejim 2,072 -> 2,287 (2,997).
- **The hops are not.** Airborne share against the default, paired by seed: runner +0.8 +- 0.8 (Dantooine), -0.2 +-
  0.5 (Kejim); no role moves beyond spread; all still 1-5 points under the control. Runner walking-search hops 112 ->
  123 a Dantooine world (control 145); ground before each 1.54 -> 1.56 s (1.19).
- **Why not:** a walking decision commits a hop at the same rate in every arm (runner 31-35%, defender 18-23%, chaser
  22-27%, camper 22-28%), so hops follow decisions, and decisions are still short of the control (Dantooine runner 759
  control, 588 default, 665 with the fix). The shortfall matches the deferrals that miss their frame: about 530 late a
  Dantooine world (320 still judging), with 2,626 chunk frames starved of the shared pool (`bot_moveWalkPool` 12).
- **Outcomes** (16 paired worlds a map against `dropfix-20261005-a/defer`): Dantooine team captures -0.07 +- 0.98, no
  metric beyond spread. Kejim team captures -1.73 +- 0.90 and **camper captures -1.73 +- 0.44 (3.9 se)**: 1.23 a world
  against 2.77-3.51 in each of the five other retained Kejim arms. Not explained.
- **Timing** (Dantooine, one world at a time, same session): off seed 1 mean 5.15 ms, p99 30.8, 459 frames over 25 ms;
  on seeds 1/2 mean 5.51/5.77, p99 30.2/29.8, 482/494 over 25 ms; Pmove steps +18-24%.

Not adopted (default stays 0): it restores the rhythm without the hops, costs about 7-12% mean frame time, and Kejim's
camper captures fall.

## Kejim camper captures: a counting artefact (owner: option 2, 2026-10-05)

Outputs `.codex-research/cadence-20261005-b/camper-touches.txt`, `camper-symmetric.txt`, `true-captures-rescore.txt`
(retained summaries only, no runs).

**`laps_compare.py`'s capture metrics count flag touches, not captures, in two-sided worlds.** `team_caps`,
`runner_caps` and `camper_caps` come from `summary.json`'s `team_captures`: touches of `team_CTF_blueflag` by
`Runner_v2` and `Camper_v2`. In a one-sided world those are captures; in a two-sided world a blue bot touching its own
flag also returns it when dropped (the tool's own comment says so). `defense_laps.py` already records true captures,
per bot from the scoreboard (`captures_by_name`, native PERS_CAPTURES); `laps_compare`'s `blue_caps`, `red_caps` and
`all_caps` use them. `camper_caps` is also the blue camper alone.

Kejim, cadence fix against the default, per world (paired by seed):

| | default | change |
|---|---|---|
| blue camper's touches (`camper_caps`) | 2.96 | -1.73 +- 0.44 |
| of which true captures | 1.48 | -0.74 +- 0.21 |
| of which returns | 1.48 | -0.99 |
| red camper's true captures | 1.11 | +0.31 +- 0.47 |
| both campers' true captures | 2.59 | -0.43 +- 0.56 |
| all true captures | 8.27 | +0.49 +- 0.73 |

The two campers run the same code; the side split is within what 16 worlds give, and the symmetric measure moves
within spread. Camper goals (retake, carry, take) are unchanged.

Re-scored on true captures, no deferral decision's pair moves beyond 2 se on either map:

| pair (per world, arm - base) | map | touches (`team_caps`) | true blue | true all | true runners | true campers |
|---|---|---|---|---|---|---|
| drop deferral, pre-fix | Dantooine | -1.47 +- 0.84 | -0.49 +- 0.75 | -0.11 +- 1.07 | -0.42 +- 1.00 | +0.25 +- 0.49 |
| drop deferral, with fix | Dantooine | -0.42 +- 0.91 | +0.13 +- 0.71 | +0.26 +- 0.88 | -0.36 +- 0.74 | +0.56 +- 0.60 |
| walk deferral, no rescue | Dantooine | -1.11 +- 1.08 | -0.55 +- 0.93 | -1.04 +- 0.78 | -0.55 +- 0.77 | -0.31 +- 0.39 |
| walk deferral, no rescue | Kejim | -1.17 +- 0.59 | -0.06 +- 0.55 | +0.80 +- 0.80 | +1.17 +- 0.61 | -0.43 +- 0.55 |
| walk deferral + rescue | Dantooine | -0.31 +- 0.92 | -0.37 +- 0.61 | -0.68 +- 0.92 | -0.43 +- 0.77 | -0.06 +- 0.41 |
| walk deferral + rescue | Kejim | -0.25 +- 0.91 | +0.49 +- 0.61 | +0.80 +- 0.84 | +0.74 +- 0.74 | -0.06 +- 0.44 |
| cadence fix | Dantooine | -0.07 +- 1.06 | -0.00 +- 0.76 | +0.30 +- 0.74 | +0.49 +- 0.63 | -0.31 +- 0.50 |
| cadence fix | Kejim | -1.73 +- 0.98 | -0.56 +- 0.46 | +0.49 +- 0.73 | +1.04 +- 0.65 | -0.43 +- 0.56 |

The drop deferral's pre-fix "runner captures -1.16" and the walk deferral's "Kejim camper captures -1.3" were touch
counts; on true captures neither stands out. The cadence fix's outcome reading changes from "Kejim camper captures
fall" to "no capture change"; its other results (rhythm restored, hops not, frame cost) stand.

## Capture metrics fixed in `laps_compare.py` (owner: option 1, 2026-10-05)

Two-sided worlds (a `Runner2_v2` joined) now score `team_caps`, `runner_caps`, `camper_caps` and `pickups` as true
counts, each the mean over the two teams (the same roles on both, one team's scale): captures from the play CSV's
native PERS_CAPTURES per client, pickups as enemy-flag touches by runners and campers. New `team_returns`: own-flag
touches less captures, a team, every role. `conversion` follows (true captures per pickup). One-sided worlds keep
their definitions (their flag never drops, so touches are captures): output on `camper-20261002-b/ce` against
`handoff-20261002-a/hd` is byte-identical to the previous tool, text and `--json`. Tests:
`tools/test_laps_compare.py` (a synthetic two-sided world with captures and returns, and a one-sided one).

Comparison files written before this change (`compare.txt`, `laps-compare.txt` in arms up to 2026-10-05) keep the old
meaning: two-sided `team_caps` there is the blue team's touches. The cadence pair on the new metrics
(`laps-compare-v2-dan.txt`, `laps-compare-v2-kej.txt` in `.codex-research/cadence-20261005-b/`; its `laps-compare.txt` is the old tool):

| per world, a team | Dantooine default -> cadence | Kejim default -> cadence |
|---|---|---|
| team_caps | 4.72 -> +0.09 +- 0.32 | 4.04 -> +0.30 +- 0.38 |
| runner_caps | 3.33 -> +0.24 +- 0.31 | 2.75 -> +0.52 +- 0.29 |
| camper_caps | 1.39 -> -0.15 +- 0.25 | 1.30 -> -0.22 +- 0.27 |
| pickups | 27.25 -> +0.05 +- 0.91 | 29.89 -> -0.21 +- 0.78 |
| team_returns | 16.92 -> -0.19 +- 0.75 | 15.30 -> +0.32 +- 0.82 |

Pooling both teams about halves the standard error: the difference in team captures has se 0.32-0.38 against 0.57-0.63 for one team's true captures (`blue_caps`) and 0.98-1.06 for the old touch count.

## Hop flow, deferral off and on; owner accepts the hop loss (2026-10-05)

Owner asked to see the flow, not shares: Dantooine seeds 1-12 run with `bot_moveWalkDefer 0` on
`cadence-build-20261005-b` (`.codex-research/flow-20261005-a/walkoff/`, `run.sh`), paired with the default
(`dropfix-20261005-a/defer`); seeds 13-16 failed to start when C: filled (below). Script `flow.py` (`flow.txt`), page
`hop-flow.html` (published artifact: speed timelines per seed and runner, landings by what followed, speed and
takeoff-speed distributions). Each pair is identical for its first 2-3 s, then two games from one start.

Per world, both runners, on - off (paired, se): airborne -5.1 +- 1.1 points; hops -35 +- 16 (530 -> 496); chained
hops (under 100 ms on the ground) -22 +- 11; hops after carrying speed (never under 400 u/s) -8 +- 3, after slowing
(100-400 u/s) +1 +- 5, from a stop (under 100 u/s) -5 +- 3. Ground seconds before a hop: carried +8.7 +- 2.0, slowed
+10.5 +- 4.3, stopped +5 +- 10. Takeoff speed by kind unchanged (median after a chained hop 645-646 u/s). Stretches
over 5 s without a hop 10.0 -> 12.3 a world. Mean speed alive 384 -> 375 u/s (-9 +- 8).

The deferral does not make the runner halt: it chains fewer hops and runs longer on the ground between them, mostly
still moving. **Owner, 2026-10-05: not ideal, accepted for the performance gain.** Walk and drop deferral stay
default; `bot_moveWalkCadenceStart` was removed from the source (never committed; `cadence-build-20261005-b` holds it).

## C: filled by staging folders; guards added (owner, 2026-10-05)

Since 2026-09-26 worlds stage on C: (`%LOCALAPPDATA%/xenjk-play`) and `defense_laps.py` never removed the staging
folder after copying the world to D:. 3,616 folders (212.8 GiB) filled C: and stopped four worlds. Audit
(`flow-20261005-a/staging_audit.py`): 3,560 copied, 27 uncopied, 29 `play-vs-runner`. Owner approved deleting the
duplicates: `flow-20261005-a/staging_cleanup.py` re-checked each against its retained world (session.json naming the
folder, `live/`, `summary.json`) and deleted 3,558 (226 GB); 29 (27 uncopied, 2 failing the re-check) moved to
`.codex-research/staging-orphans-20261005/` without binaries; play sessions untouched; log
`.codex-research/staging-cleanup-20261005.log`. C: free 0 -> 228 GB. Guards: `world_retention.require_free` (20 GB,
`XENJK_MIN_FREE_GB`) in `defense_laps.py` and `play_vs_runner.py`; `defense_laps.py` removes a world's staging after its
copy (`--keep-staging` keeps it), checked on a 30 s world; tests in `tools/test_world_retention.py`; AGENTS.md rule.
