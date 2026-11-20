# Searching more often does not help: the waiting frames are not dormancy

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> This rejected pilot does not globally refute a search-rate improvement. Its
> three-run arms sampled different uncontrolled starts. The underlying 54.2%
> waiting claim includes terminal walking; absence of a commit does not establish
> failed planning. Timing tables use the legacy analysis window rather than
> completion. Keep the experimental change reverted, but treat availability versus
> latency and the "only remaining lever" claim below as hypotheses, not settled
> causes.

Recorded 2026-09-20. Build `.codex-research/search-rate-build-20260920-a`;
evidence `.codex-research/search-rate-20260920-a`. The source change is
**reverted** and all 1,117 manifest sources hash-match the reference build.
Nothing is installed.

## The hypothesis, and where it came from

[The leg budget](2026-09-20-leg-budget.md) found that
`waiting_for_a_committable_plan` owns **54.2 %** of grounded frames, 473 frames
across eight worlds and 6,133 units/sec of lost speed. Measuring the search
cadence inside those runs gave: **55 searches over 373 waiting frames**, the
search running on 15 % of them, with the modal gap exactly `MOVE_GAP_RETRY`
(17 of 37 gaps at 250 ms).

That was read as dormancy - the bot standing on the ground shedding 15 % of its
excess speed per frame while not even looking for a way to leave - and the
prediction was that raising the rate would cut the waiting frames.

`StraightEligible` gates a non-landing grounded frame twice: `MOVE_GAP_RETRY`
(250 ms after a failed search) and `MOVE_GAP_CADENCE` (200 ms since any
search), so the effective floor is 250 ms, which is why that gap dominates.
`bot_moveSearchRate` replaced both with one millisecond value; the gap search
kept its own retry, untouched.

## The treatment applied, and it is visible

Search gaps inside the leg, first run of each arm:

| Arm | Searches | Gap histogram |
| --- | ---: | --- |
| control | 16 | one 250 ms; the rest are the 1,650-1,975 ms flights between landings |
| rate 0 | 55 | **40 gaps of exactly 25 ms** |
| rate 100 | 26 | **8 gaps of exactly 100 ms** |

The knob does what it says. Two control runs and one rate-100 run also
reproduce retained world `confirm-1-r7` byte for byte, so the flag is inert
when unset and the arms sample the same outcome set.

## The pilot, declared before it ran: three runs per arm, alternating

| Arm | Legs (ms) | Median | Mean | Waiting frames | Run-up frames |
| --- | --- | ---: | ---: | ---: | ---: |
| control | 16,275 / 16,275 / 19,000 | 16,275 | 17,183 | 59.0 | 20.0 |
| rate 0 | 20,525 / 18,525 / 17,975 | 18,525 | 19,008 | 52.0 | 30.7 |
| rate 100 | 19,250 / 19,150 / 16,275 | 19,150 | 18,225 | 58.7 | 25.7 |

No arm improves. Both rate arms are worse at the median and the mean, in the
same direction at two different rates. Waiting frames barely move - 52.0 and
58.7 against 59.0 - while run-up frames rise in both arms.

Within-arm spread is 2,750 ms for the control and 2,550 for rate 0, comparable
to the between-arm difference, so this pilot is strong enough to refuse the
change and not strong enough to call it harmful. Either way there is no
benefit to pursue.

## Why, and it was already in the evidence

The waiting frames are not dormancy. During them the search already judged
**1,029 candidates across eight worlds and admitted none**, and
[the trigger classification](2026-09-20-trigger-classification.md) showed why:
the refusals are flights into the map's hurt volume. Judging more candidates
drawn from the same states cannot create an option the physics refuses. Rate 0
ran roughly twice the searches and still could not commit earlier; what rose
instead was the run-up, which is the bot doing the one thing that works -
shedding speed until a hop is survivable.

**The 15 % figure was misread.** A low sampling rate over an interval whose
sampled outcome never varies is not evidence that sampling is the constraint.
The search was running often enough to establish that nothing was there.

## What this retires

- **These two grounded re-search-rate variants.** Neither established a useful
  improvement in the small unpinned pilot; both remain reverted. This does not
  isolate a general causal effect of `MOVE_GAP_RETRY` or `MOVE_GAP_CADENCE`.
- **Reading absent commits as measured latency.** The corrected budget separates
  uncommitted ground; recorded refusals establish unavailable sampled candidates,
  not the absence of every possible continuation.

## What is left

The sampled refusals suggest investigating the state chosen before a hazardous
landing. Continuation ranking is one candidate, not the only remaining lever.
The pilot does not show that every possible downstream correction fails.

That is the handoff's bounded continuation ranking, and it now has the concrete
selection failure it was gated on. The cheap probe first, because it needs no
build: `bot_moveExitSpeed` is currently 2000, and `ExitCredit` adds exit speed
straight into the score, so the ranking actively rewards the landing speed that
these refusals then reject. Sweeping that cvar and reading the result off the
leg-budget owner split - rather than off leg time, which has defeated three
pilots in a row at this sample size - tests whether the credit is buying speed
the route cannot absorb.
