# Half of every candidate set is refused for touching a trigger, and the item cvar does not help

Recorded 2026-09-21, under the owner's direction to aim the next speed work
at start classes 150 and 125 - 77 of the 93 declared weight - and at
**offering better continuations** rather than choosing better among the same
ones. This measures the observed first stopping reasons and closes the tested item
bypass lever. It does not measure the speed available behind those refusals.

Review follow-up: [next-goal handoff](2026-09-21-trigger-shadow-handoff.md).
Measure contact-test disagreements in shadow mode before changing admission.

Protocol declared before collection, driver, analyser and evidence in
`.codex-research/offer-items-20260921-a/`; the summary quoted here is
`summary-items-1.json`. The census is read-only over
`.codex-research/prefer-ablation-20260920-a`.

## What limits the offer

Every `[SPIKE]` candidate rollout of the ablation's accepted worlds:

| Start class | Rollouts | Refused | Admitted |
| ---: | ---: | ---: | ---: |
| 75 | 4,025 | 52.7 % | 18.0 % |
| 100 | 4,040 | 57.8 % | 18.8 % |
| 125 | 3,263 | 47.1 % | 20.0 % |

**Every refusal is `unsupported=256`, the trigger contact: 5,995 of 5,995.**
Not one is solid, body, material, liquid, movement type, active power, hand
extend or command time. About half of every candidate set is discarded for
touching a trigger volume, and the physical failures the search was built to
find - falls, failed onward walks, landings off the graph - are the small
buckets beneath it.

This is the same mechanism that made
[the parity harness inert](2026-09-20-parity-zero-comparisons.md). It is
the most frequent first stopping reason in this sample is the forecast's
trigger rule. A stopped rollout has not been tested for later physical failures
or useful completion, so this does not identify the dominant speed bottleneck.

In the class-150 diagnostic world the contacts split `trigger_hurt` 304,
`team_CTF_blueflag` 139, `team_CTF_redflag` 53, `item_medpak_instant` 59,
`trigger_push` 1. Flag contacts that reach the objective are already rescued
as arrivals by `StraightArrives`, which reads the candidate's own rollout
inside the judging loop. Item contacts are not rescued.

## The item cvar is refuted at both heavy classes

`bot_forecastItems` exists, is off by default, and makes a weapon, ammo, armor
or health pickup no longer a refusal. Three repetitions an arm, arms
alternated, **12 cells of 12, none incomplete, 25 native worlds**, one
distinct completion per cell, zero deaths, movement owner only:

| Start class | `bot_forecastItems=0` | `=1` | Delta |
| ---: | ---: | ---: | ---: |
| 150 | 17,575 ms | 17,575 ms | **0** |
| 125 | 15,925 ms | 15,925 ms | **0** |

Both controls reproduce the preference collections' `StatePrefer=1` cells to
the millisecond, on a different build - this collection runs on
`.codex-research/parity-fix-build-20260920-a`, which carries the parity
repair - so the null is measured against a control that is known good.

The offer barely widens either: refused rollouts are 49.6 % against 49.8 % at
class 150, and 35.9 % against 34.0 % at class 125.

**Why it does nothing is the useful part.** Refusals are not independent. In
the matched diagnostic pair at one start level, turning the cvar on removed
all 59 medpak contacts and raised `trigger_hurt` contacts from 304 to 334:
a rollout that used to stop at a pickup now runs on to the pit and is refused
there instead. Removing one source of contact mostly defers the contact to
the next one.

## Parity, now that it reports

Every world of this collection carries the repaired harness: **8,510 frames
compared across the four arms, zero mismatches, zero forecast refusals**, on
worlds collected for another purpose entirely. That is the qualification
signal accumulating as ordinary evidence rather than as a special run.

## What this points at, and what it needs

`trigger_hurt` is 55 to 60 % of contacts and is untouched by any existing
cvar. It is a real hazard - the pit - so it cannot simply be ignored, and
[the trigger classification](2026-09-20-trigger-classification.md) already
established that the sampled contacts are genuine.

What is worth examining is the **test**, not the hazard. `TouchesTrigger` is a
swept axis-aligned-box overlap check with fixed extents of 15 units laterally
and 24 to 40 vertically, standing in for the player hull; it is not an exact
segment/brush query. In the diagnostic world
only **17 of 304** `trigger_hurt` contacts had their logged endpoint inside
the logged entity bounds; the rest were inside the margin alone, by a median of 10 units
and at most 23. That does **not** make them false - the margin exists because
the hull is not a point, and the logged endpoint is one end of the segment the
test actually uses - but it does mean the refusal rate is set by an
approximation. It does not establish the false-positive rate: an origin
outside the bounds can still have a hull that touches, and an endpoint alone
does not describe the movement interval. Compare both tests at the native
contact boundary without changing the refusal decision first.

The next task is the default-off shadow diagnostic in the linked handoff.
Only measured useful disagreements justify proposing a contact-test change.

## What this does not establish

- Two start classes of one case, one map, one build, three worlds an arm,
  pinned cells rather than counterbalanced pairs.
- The census pools `StatePrefer` on and off arms of another collection, whose
  trajectories differ; the percentages are its own, not this collection's.
- The deferral mechanism is one matched pair at one start level, not an arm.
- Nothing is installed. `bot_forecastItems` stays off, which is what this
  measurement supports.
- No speed win. This closes a lever and maps where the offer is lost.

## Reproduce

```powershell
python .codex-research/offer-items-20260921-a/analyse.py --out <new unique output.json>
```

One world of one arm, from the workspace root, on an unused port:

```powershell
python tools/movement_bench.py `
  --build .codex-research/parity-fix-build-20260920-a `
  --out <new unique path> --filter '^red_out_L1_stand$' `
  --edges --parity 256 --parallel 1 --port 57600 `
  --graph .codex-research/move-explore-20260916-b/seed.txt `
  --momentum-trace --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 `
  --set bot_moveStatePrefer=1 --set bot_moveReroute=1 `
  --set bot_moveExitSpeed=500 --set bot_moveSuccessorAim=1 `
  --set bot_forecastItems=1 `
  --corner-class 150 --corner-class-min 3000 --corner-class-max 5500 `
  --corner-level-attempts 1
```
