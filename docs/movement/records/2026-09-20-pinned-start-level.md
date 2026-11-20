# Pinning the fixture's start level, and what it costs

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The pinned 19,050 and 16,450 ms outcomes here are official completion times.
> The original finer-sweep 19,011 and 16,275 ms figures were shorter analysis
> windows, so equality must be checked using the same metric. Pinning verifies
> release time and retains misses; it does not prove treatment-independent timing
> or justify confirmation at only one favorable start. Declare several strata,
> including 3275, and report any unreachable stratum as incomplete.

Recorded 2026-09-20. **Tool change only**: `tools/movement_bench.py` and
`tools/movement_bench_lib.py`. No native source change, no rebuild; every
world below is the frozen reference build
`.codex-research/successor-aim-build-20260920-a`, whose 1,117 manifest sources
the runner re-verified against the checkout on every run. Evidence in
`.codex-research/pin-check-20260920-a/` and `.codex-research/pin-calib-20260920-a/`.

[The start-level record](2026-09-20-start-level-determines-the-leg.md) is the
reason for this change: `corner_level` decides the leg, and nothing was
controlling it.

## What was added

`--corner-level MS` holds `bot_corner_start` until the world is standing on
that level, sends it, and then **verifies the level the world reports**. A
world that lands anywhere else is retained under `<case>.miss-N` and replaced
by a fresh one, up to `--corner-level-attempts` (default 4). `result.json`
gains `corner_level_target`, `corner_level_observed`, `corner_level_lead_ms`
and `corner_attempt`, and the campaign summary now reports each case's
`corner_level` without opening the world.

Unset - which is the default, and what acceptance floors should use - the
runner behaves exactly as before: one world per case, the same retained
layout, and the start level sampled rather than chosen.

## How the aim was calibrated

The command executes on arrival, so the runner has to know when to send it.
Three worlds pinned at a level the world had already passed reported the same
thing: **the start lands on the level of the last play-log row the runner has
seen**, 3,350 observed and 3,350 landed, three times out of three. So the
default lead is zero and `send` means the world is standing on the target
frame now. `--corner-level-lead` remains for re-tuning on a slower host.

The runner's clock is the play log, and that file is flushed in bursts of
roughly 200 ms rather than per frame. Between flushes the runner is blind, so
it cannot wait for an arbitrary frame - it can only recognise the frames it
happens to see. That is the whole limitation of this approach, and it is why
the pin verifies rather than trusts.

## What is reachable, measured

| pin | worlds | landed | first-attempt sends | leg result |
| ---: | ---: | ---: | --- | --- |
| `corner_level` 3,350 | 6 | **6 of 6** | 4 of 6, 6 hits in 7 sends | **19,050 ms every time** |
| `corner_level` 3,325 | 4 | **3 of 4** | 1 of 4 | **16,450 ms every time** |

Both figures are `time_ms` at the reference configuration
(`bot_moveExitSpeed=2000`), and both match the official completion times in the 100-world sweep
for those strata. The point is not the numbers but their spread: **zero**,
where the same configuration unpinned spreads 4,300 ms.

3,350 is the modal level the runner first sees, so it is cheap. 3,325 is one
frame earlier and mostly gone before the runner looks; catching it needed the
post-add poll shortened from 500 ms to 50 ms, which raised it from 1 world in
4 to 3 in 4. Levels below 3,325 were not reached at all.

So two strata are pinnable today: the tap start at 3,350, and the fast run-up
start at 3,325 at roughly three times the cost. That answers the caution the
start-level record raised - a pin that offered one phase would measure one
phase - but only just, and it is worth saying plainly that **two is not many**.

> **Superseded in part, 2026-09-20.** The reachability limit is the play log's
> 250 ms pre-start cadence: a world starts only on its own ladder `L0 + 250k`,
> and `L0` varies world to world, so which exact level is reachable is a
> property of the drawn world rather than of the level. Pinning the start
> *class* (`corner_level mod 200`) instead reaches every stratum, including
> the slow one below 3,325. See
> [the pinned confirmation](2026-09-20-exit-speed-pinned-confirmation.md).

## What this does not do

- It does not make the fixture deterministic. It rejects worlds until one
  starts where it was told, which is a different and weaker thing; a run that
  cannot hit the pin in four attempts comes back `incomplete`, as the fourth
  3,325 world did.
- It does not touch the native side. A corner start scheduled by the server
  for a declared level would make this exact rather than statistical, and
  would reach levels the runner cannot see, but that is a source change and a
  rebuild, and nothing measured so far needs it.
- It does not change any retained result. Every earlier world was collected
  unpinned and stays comparable to every other unpinned world.
- It changes `runner_sha256`, so worlds collected after this are bound to a
  different runner than worlds collected before it. The build manifest, which
  is what binds the measured code, is untouched.

## Regressions

Ten new tests in `tools/test_movement_bench.py` over the two pure helpers -
`latest_play_level`, which skips the torn final line the server is still
writing rather than guessing at it, and `corner_pin_state`, which decides
between waiting, sending and giving the world up. The movement Python suite is
112 tests and passes.

## Reproduce

```powershell
python tools/movement_bench.py `
  --build .codex-research/successor-aim-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 32700 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=2000 --set bot_moveSuccessorAim=1 `
  --corner-level 3350
```

Drop `--corner-level` for an acceptance floor. Compare arms at one pin, and
report which pin, because an arm's advantage is not the same at every start.
