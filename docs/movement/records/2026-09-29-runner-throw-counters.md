# Runner push 3 and pull 3, and a counter-aware throw forecast (2026-09-29)

Owner: the runner has Force ranks level 3 in push and pull.

## Before

The runner's movement-only branch of `Bot2_Think` returns before the level-3
Force block that full Bot2 bots get (`ai_bot2.c`), so its ranks came only from
`tools/fixtures/strafe_play/kejim_runner.bot` and `.jkb`:
`7-1-033000000000000110`. That is levitation 3, Speed 3, saber offense and
defense 1, with no push, pull or absorb.

## Change

- **Runner:** `kejim_runner.bot` `forcepowers` and `kejim_runner.jkb`
  `forceinfo` become `7-1-033330000000000110`, adding push 3 and pull 3 at 10
  rank points each. That totals 40-42 of the 100 points rank 7 allows, and the
  server sets `g_maxForceRank 7`. Every tool that stages the runner copies this
  file: `movement_bench.py`, `play_vs_runner.py`, `movement_explore.py`,
  `route_approve.py` and `route_ownership_probe.py`. The pre-rewrite
  `kejim_defense_play.py` writes its own runner file and is unchanged.
- **Defender forecast.** `ThrowVelocity` in `bot2_defender.c` assumed a
  target without throw powers. It now takes `CounterLevel`: ForceThrow's
  counter rule applied to the runner.
  - **Counter level:** the runner's push or pull level, one less while it
    presses a move.
  - **When it applies:** only on the ground, with its hand free, no weapon
    refire or charge, and no counter in the last one.
  - **Effect on the throw:** an equal or higher level cancels all of the
    throw's 256 x level. A gap of 1, 2 or 3 levels cuts it by 80, 40 or 20%.

  The ground test uses the frame the defender perceives; everything else uses
  the runner's current state. Both the lethal-throw search and the shadow
  forecast use it.
- **`HIT` line:** it now also logs `counter` (the runner's counter animation
  fired), `predicted_counter`, `model_h` (the forecast's horizontal speed from
  live positions at the throw) and `native_h`.

## Engine facts (w_force.c, ForceThrow and CanCounterThrow)

- **An airborne target cannot counter.** Every throw at a hopping runner lands
  in full, which is how the owner's six pit kills happened
  ([human defense session](2026-09-29-human-defense-session.md)).
- **A countered throw still sets the target's velocity,** to at least 16 u/s
  along the throw line, with a 128 u/s hop when it had no vertical speed. A
  counter stops the runner's momentum instead of preserving it; it prevents
  the displacement.
- **A moving target defends one level lower.** The runner nearly always
  presses a move, so against a level-3 throw it counters at level 2, which
  removes 80% of the throw. After losing 0.7 x distance, that is almost always
  the 16 u/s floor.
- **No knockdown.** With push or pull 3, a level-3 throw can no longer knock
  the runner down (the reach is `64*((3-2)-1)` = 0 u).
- **Cost.** A counter costs no Force, and it does not block Speed. Its hand
  animation (400 ms for a pull, 1,000 ms for a push) blocks weapon fire,
  including the carrier's staff swing.

## Verification

Build `counter-build-20260929-c` (a copy of `-b`, which never received the
`HIT` fields, and `-a`). Worlds and `compare.py` are in
`.codex-research/counter-20260929-a/`.

| Arm | Against | Result |
| --- | --- | --- |
| No defender, Kejim class 150, `--skip-twins` | `ctl-nodef-c150`, collected on `roles-build-20260929-c` with the old runner file before the edit | 24 of 24 identical. The ranks do not touch movement. |
| Both defenders, pickups, class 150 | `roles-20260929-a/pick-both-c150` | stopped before the grab 4 → 4; carriers killed 5 → 6; throws 65 → 58, hits 58 → 52 |
| Both defenders, returns, class 150 | `roles-20260929-a/ret-both-c150` | stopped 0 → 0; carriers killed 2 → 2; throws 23 → 22, hits 22 → 21 |

**Forecast against native** (73 hits in the two defender arms):
- **Counters:** 7 hits were countered natively, leaving the runner at a
  median of 15 u/s, against about 400 u/s uncountered.
- **Counter predictions:** the forecast predicted the counter for 70 of the
  73. The countered hits it got right matched to 0.1 u/s.
- **Speeds:** on uncountered hits the forecast is within a median of 5 u/s of
  native. That is one frame of the runner's own air steering.
- **The misses** are 3 hits (two of them the same red return throw, which
  both arms run). The runner countered but was predicted not to: the forecast
  saw it airborne at the throw, and it was grounded when ForceThrow ran. This
  is a one-frame ordering edge, left open.

**Why so few counters.** Only 7 of 73 hits were countered because the
defenders' throws mostly land on a runner in the air. Lethal throws target its
flights, and carrier pulls catch it mid-hop. Outcomes did not change beyond
one carrier kill. Counters will matter once throws aim at the runner on the
ground; the defenders' next behaviours (mid-hop throws) do not.
