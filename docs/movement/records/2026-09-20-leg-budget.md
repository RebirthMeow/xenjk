# What the outbound leg's eighteen seconds are made of

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The tables below preserve the original LEG_BUDGET1 analysis, not current
> completion statistics. Its window ends before completion; its speed integral
> does not establish a fixed route. The 473-frame / 54.2% waiting category includes
> 59 terminal frames with no recorded candidate search, so it cannot measure
> failed-planning time. Previous airborne samples were also incorrectly used as
> touchdown speed. The speed-preservation calculation is withdrawn as a bound.
> LEG_BUDGET2 distinguishes analysis and completion time, uses first-grounded speed,
> separates uncommitted ground, and validates both coverage endpoints. Regenerate
> into a new output; do not compare corrected owner counts with these legacy ones.

Recorded 2026-09-20. Read-only analysis of retained evidence. No native run,
no source change, no rebuild. Tool: [movement_leg_budget.py](../../../tools/movement_leg_budget.py);
reports in `.codex-research/leg-budget-20260920-a/`; inputs are the eight
confirmation worlds of each arm in `.codex-research/successor-aim-20260920-a/`,
bound by hash in each report.

## The leg is a fixed distance, so the whole question is mean speed

The eight aim-on worlds cover **7,161 units** of speed integral with a spread
of **2.2 percent**. The route does not vary; leg time is distance over mean
speed and nothing else.

| | aim on (8 worlds) | aim off (8 worlds) |
| --- | ---: | ---: |
| Airborne frames | 84.7 % | 84.0 % |
| Grounded frames | 872 of 5,694 | 962 of 6,000 |
| Best mean speed | 442.1 units/sec (16,250 ms) | 402.9 units/sec (17,775 ms) |
| Worst mean speed | 370.6 units/sec (19,275 ms) | 350.8 units/sec (20,325 ms) |
| Highest speed reached | 582.9 units/sec | - |

**15,000 ms over 7,161 units requires a mean of 477 units/sec.** The best leg
already means 442 and the bot repeatedly reaches 583, so the target is not
excluded by the physics. It is not reached by any observed leg either.

The bot is not slow because it cannot accelerate. It accelerates to 470-583
repeatedly and then loses all of it.

## Where the grounded frames go

Each grounded run is split at the commit that ends it: frames before that
commit are the search finding nothing admissible, frames after it are the
committed plan's own run-up. Aim-on totals:

| Grounded-frame owner | Runs | Frames | Share | Speed lost |
| --- | ---: | ---: | ---: | ---: |
| `waiting_for_a_committable_plan` | 26 | 473 | 54.2 % | 6,133 |
| `runup_before_launch` | 45 | 213 | 24.4 % | 5,515 |
| `tap_on_landing_frame` | 102 | 102 | 11.7 % | 2,895 |
| `leg_start_stand` | 8 | 80 | 9.2 % | 0 |
| `other_delay_before_launch` | 4 | 4 | 0.5 % | 0 |

The 102 single-frame touchdowns cost 28 units/sec each: that is one frame of
friction and it is what a healthy landing looks like. Every run-up is 4, 5 or
6 frames, which is the 24-unit run-up walked off at wishspeed; each costs
123 units/sec on average and the largest observed single cost was 333, from
582.9 down to 250.4 in four frames.

Successor aim's improvement appears in exactly this accounting: 90 fewer
grounded frames than the aim-off arm, 47 of them waiting and 42 run-up. The
metric and the known result agree, which is the reason to trust the metric.

## Nothing is admitted at the frames that cost the most

During the 473 waiting frames the search judged 1,029 candidates and admitted
**none**. Why the rollouts stopped:

| Outcome | Count |
| --- | ---: |
| `refused`, unsupported bit 256 (trigger contact) | 731 |
| `fell` | 154 |
| `corridor` | 110 |
| `slower`, unsupported bit 256 | 17 |
| `no_progress` / `no_path` / `no_onward` | 17 |

**Unclassified trigger contact is the largest refusal class at the most
expensive frames in the leg**, 748 of 1,029. Per
[LESSONS.md](../LESSONS.md), bit 256 is `BOT2_FORECAST_TRIGGER`: the rollout
ended touching a trigger entity, which on Kejim is usually an item, the enemy
flag, or the map's hurt volume after a 1,100-unit fall. Only the last is a
failed maneuver, and the forecast does not distinguish them.

A worked instance, world `confirm-1-r1` at relative 10,725 ms, landing at
582.9 units/sec, all fifteen candidates logged:

- Every `r0=0` candidate refused, `unsupported=256`, `land0=-1`, `done=-1`:
  launched at 545 and ran 84 frames without recording a landing.
- Every admitted candidate carried `r0=24`, launching at 239-247 because the
  run-up had already shed the speed to wishspeed.
- The commit took `r0=24`, `reissued_up=0`, and launched four frames later at
  250.4.

The candidate the bot needs is refused on a contact nobody has classified, so
the bot walks 333 units/sec away and takes the one that remains.

## Historical speed-preservation counterfactual (withdrawn as a bound)

Holding each touchdown's entry speed through its waiting and run-up frames and
recovering the same distance:

| World | Measured | Counterfactual |
| --- | ---: | ---: |
| Best (r3 / r6) | 16,250 ms | 15,690 ms |
| Worst (r4 / r5) | 19,275 ms | 17,717 ms |

The original claim that this is a **lower bound on saving is withdrawn**. The
following rationale is retained for audit, not as a supported bound. It removes the
speed lost during those frames and gives no credit for the re-acceleration
that would no longer be needed: after a repaired landing the bot enters the
next flight near 450 rather than 250, and `speed^2` rises about linearly in
air frames from there. It equally assumes nothing about whether a refused
flight was physically available, which is the open question above. Distance
here is the speed integral, not the route's own measured length.

So the first-order removal of the grounded loss does not by itself reach
15,000 ms on any world. The saving that would reach it has to come from the
flights that follow, and those cannot be priced without a rollout.

## What this settles and what it does not

Settled, from measurement:

- The route is a fixed 7,161 units and 15,000 ms needs a 477 units/sec mean.
- 84.7 percent of the leg is already airborne; air-time fraction is not the
  headline problem, and the 15.3 percent on the ground is where the loss is.
- 78.6 percent of grounded frames belong to two owners, waiting and run-up,
  and both are decisions rather than physics.
- The fast and slow worlds separate on those two owners: 39-40 waiting and 13
  run-up frames in the 16,250 ms worlds against 89-97 and 34-39 in the
  19,025-19,275 ms worlds.

Not settled:

- Whether any refused trigger contact was survivable. **Nothing here is an
  argument for an admission exception.** The contacts need classification.
- Whether a shorter first-hop aim exists at high landing speed.
  `StraightSearch` computes `d0 = (0.695 * v0 + 76) * aim` with
  `straightAims[] = {.85, 1.0, 1.15}` and, for a zero run-up,
  `v0 = max(wishspeed, live speed)`. At a 545 units/sec landing the nearest
  aim point is therefore 387 units away and the furthest 524. The vocabulary's
  reach grows with exactly the speed it most needs to preserve. This is a
  hypothesis from the formula and the worked instance, not a measured cause.
- The whole-trip effect of repairing either owner. Both need a pilot.

## Reproduce

```powershell
python tools/movement_leg_budget.py `
  .codex-research/successor-aim-20260920-a/confirm-1-r*/red_out_L1_stand `
  --out <new unique path>
```

`--target-ms` sets the leg time the required mean speed is reported for; it
defaults to 15,000. The tool refuses a leg whose 25 ms momentum coverage is
incomplete rather than interpolating across the gap. Regressions:
`tools/test_movement_leg_budget.py`, 8 tests.
