# Strafe skill lever (2026-09-26)

Owner: the runner strafes too well for normal play; take its air strafe from about 88% of
perfect to about 75%.

## Change

- `bot2_moveIntent_t.strafeSkill` (`bot2_movecontrol.h`): with a held strafe side, the share
  of the best per-command gain the view takes. Below 1 the controller aims wide of the best
  view (the usual human miss) so that a command of add a, whose wish has travel component u,
  gains 2ua + a^2 = k times the best gain, a(2w - a) (2va + a^2 below v = w - a). 0 or 1 is
  the old controller unchanged.
- `bot_moveStrafeSkill` (percent) is read once with the flight mode and set on all three
  held-side intents in `bot2_move.c`, so forecasts and execution use the same value.
- Default: first 100 with play at 81; then **90 as the DLL default** (owner, 2026-09-26, after
  playing 90 and 80; build `skill90-build-20260926-a`), so play, fixtures and gates all run 90.
  Gates re-taken on it: `.codex-research/gate-20260926-b`.

Build `.codex-research/strafeskill-build-20260926-a`.

## Calibration

Runner alone on Kejim (`play_vs_runner.py --no-client`, production settings, about 7 min
each), scored over the whole server POV demo with `human_rush_study.air_strafe` (same
accounting as the human study; the trip extractor needs respawns, which a runner with no
opponent never has). Sessions `.codex-research/play-vs-runner-20260926T0944*`/`T0953*`.

| bot_moveStrafeSkill | Efficiency (25 ms) | Median 300 ms+ segment | Pickups | Pickup cycle median | Speed median |
|---:|---:|---:|---:|---:|---:|
| 100 | 89.8% | 92.7% | 15 | 27.9 s | 564 |
| 100 (repeat) | 89.6% | 91.8% | 16 | 27.2 s | 557 |
| 85 | 79.0% | 80.9% | 16 | 27.0 s | 581 |
| **81** | **74.7%** | 76.3% | 18 | 25.4 s | 611 |
| 75 | 70.3% | 71.2% | 16 | 25.8 s | 581 |

The Kejim fixture at `--corner-class 150 --skip-twins --parity 256`
(`.codex-research/strafeskill-20260926/bench`): 24/24 pass in both arms, **0 parity
mismatches** at 81. Level 1 (no Force) slows: stands +2.3 to +3.9 s (16,500 to 20,375 ms),
respawns +0.2 to +1.35 s. Levels 2 and 3 are unchanged within the quantisation (-0.7 to
+0.55 s). Sum 313.1 s to 326.6 s.

## Level-2 sweep (Kejim, class 150, `--skip-twins`, 8 cases)

Totals 100: 91.92 s, 98: 95.03, 95: 90.47, 92: 88.97, 90: 95.95, 88: 92.83, 85: 96.53,
82: 97.95, 80: 91.88 (`.codex-research/strafeskill-20260926/sweep-l2`). No trend: small
skill changes move where hops land and so which line the planner takes; each value is
deterministic per start.

## Gates at the 90 default (`.codex-research/gate-20260926-b`, 08:36-09:07)

Build `skill90-build-20260926-a` (also bots-only fixed step, threat hiding, probes off);
sources verified before and after; library `slowdrop-chain-20260924-a` **not re-explored**.

- **Level 2: every row met.** Floors 36/36 on Kejim, ctf4, Dantooine v3, zero deaths, zero
  mismatches. Stand `red_out_L2_stand` 9,400 / 9,850 / 10,500 / 10,750 ms (bound 13,000;
  at 100: 8,150 / 9,100 / 9,875 / 9,575). Coverage 36/36, median 9,450, tail 11,800.
  Cost p99 17.0 / 17.6 ms (at 100: 13.6 / 13.4), 4 and 4 frames over 25 ms, worst 142 ms.
- **Level 1: row 2 fails.** Library 14,550 / 17,700 / 17,775 / 18,150 ms, identical to the
  control at every class: median 17,737.5 (bound 16,500), tail 18,150 (bound 18,000). At 100
  the library gave 11,650 / 14,925 / 14,325 / 14,125 against controls 11,650 / 15,925 /
  14,325 / 16,175. The library now selects only `phase` on `red_out_L1_stand`: its tapes were
  explored at skill 100. Row 3 fails narrowly at class 100 (52.5 against 52.2 percent).
  Rows 1 (36/36, zero mismatches), 4 (coverage, no deaths) and 5 (cost) met.

## Library re-explored at 90; gates re-taken (`.codex-research/gate-20260926-c`, 13:04-13:32)

Re-explore `.codex-research/skill90-chain-20260926-a` (11:55-13:04; `skill90-chain-20260926-a.sh`,
explorer as before, beam 3, patience 2). Best confirmed, class 75/100/125/150: level 1
14,550 / 12,100 / 13,875 / 12,300 ms, level 2 9,025 / 8,925 / 8,225 / 9,775 ms; the library
reproduced every one twice; Kejim floor with it 36/36.

- **Level 1 row 2 met:** library 14,550 / 12,100 / 13,875 / 12,300, median 13,087.5 (bound
  16,500), tail 14,550 (bound 18,000); control median 17,737.5. **Row 3 met strictly** at every
  class (worst waiting 48.1 / 45.0 / 33.3 / 28.9 against control 48.1 / 52.2 / 58.1 / 65.5).
  Row 1: floor 36/36, zero deaths, zero mismatches, harness-on arms 514-646 comparisons, times
  equal to harness-off. Row 4: 72/72, no deaths (the library selects `exact` on
  `red_out_L1_stand` only; other arms differ by unpinned start class).
- **Level 2 rows 0-3 met:** floors 36/36 on three maps; stand 9,025 / 8,925 / 8,225 / 9,775 ms
  (bound 13,000; at 100: 8,150 / 9,100 / 9,875 / 9,575); coverage 36/36, median 9,400, tail 11,275.
- **Cost (L1 row 5, L2 row 4) met on re-measurement.** The gate's two worlds read p99 26.1 /
  26.8 ms, and five re-runs in the next 15 minutes 21.1-29.8 ms with either library. Eight
  interleaved worlds later (`gate-20260926-c/cost-recheck2`): new library 16.5 / 16.7 / 17.9 /
  16.5 ms, old library 17.6 / 17.8 / 16.8 / 17.4 ms, 4 frames over 25 ms each, worst 126-171 ms -
  the level of `gate-20260926-b` (17.0 / 17.6). The library does not change cost; the cause of
  the elevated window is not known (owner: no heavy desktop load).

## Reading

The lever does what it says to the strafe, but in production play (Force Speed on most of
every life) lap time did not move: strafe quality is a small part of the runner's speed
once it has Speed. Making it slower in play needs a different lever (Speed usage, route
choice), which is the handoff's item (3).
