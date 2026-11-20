# Straight-search rescue converts 39 blue-flag refusals; wider payoff is unmeasured

Answers the gate the [trigger shadow measurement](2026-09-21-trigger-shadow.md)
set on its own proposal. **No admission rule changed**, nothing is installed,
and no speed was claimed.

Protocol, driver, analyser and evidence in
`.codex-research/trigger-shadow-20260921-b/`; summary `summary-rescue-1.json`.
This is a **separate build** from `trigger-shadow-20260921-a` and the earlier
evidence is not carried over to it.

## The question

That measurement proposed one narrowly scoped repair - make the forecast's item
branch exclusive, as native's if/else is - and gated it on one unknown: **51 of
the 55 item fall-through refusals are `team_CTF_blueflag`, the objective**, and
`StraightArrives` already rescues a candidate whose trigger contact is the flag
at the objective. The follow-up measures that initial conversion in the
straight-search hook, not every rollout or final admission.

It could not be answered then: a shadow record carried no candidate identity.

## How the straight-search subset was measured

The first attempt tagged every rollout and joined it to its `[SPIKE]` line by a
sequence number. **That was abandoned before any world was collected.** The
straight search prints its candidates in a second loop, so every candidate in a
search shares one print-time counter and the join is not exact. The tag is
retained in the records as context; no conclusion rests on it.

What replaced it needs no join. `StraightArrives` runs **immediately after the
candidate rollout it judges**, so the search reports its own verdict back while
that rollout is still the last one run:

```c
r->outcome = RunCandidate(...);
StraightArrives(ent, r, rootGoal);
Bot2_ForecastShadowRescue(!strcmp(r->outcome, "arrives"));
```

The diagnostic files that verdict under the **ladder cell of the trigger that
caused the refusal** - the first old-contact in the deciding call, which is the
entity the shipped test returns. Nothing is matched after the fact, so nothing
can be mis-attributed, and the per-rollout flag is cleared at the start of every
rollout so a verdict can only describe its own.

## The observed initial conversions

Classes 150 and 125, **6 of 6 cells complete, none incomplete, 19 worlds**,
completion times all as predicted at 17,575 ms and 15,925 ms. Pooled over the
timed trip: 2,520 rollouts judged, 129 stopped by a refusal no native rung
reached.

| Mechanism | Initial `arrives` conversion | No initial conversion |
| --- | ---: | ---: |
| `hull_or_pad` - the item fall-through, all `team_CTF_blueflag` | **39** | **0** |
| `box_not_brush` - all `trigger_hurt` | 0 | 30 |
| `swept_not_endpoint` - enclosing-box contact; exact crossing unresolved | 0 | 60 |

**All 39 blue-flag refusals observed by the straight-search hook converted
initially to `arrives`.** The per-world counts repeat across three repetitions:
class 150 has 29/13/16 and class 125 has 14/0/14 (item/box/swept-box categories).
These are results within this instrumented subset, not proof that every
blue-flag refusal is recovered.

The hook runs immediately after `StraightArrives`, before later chain/path
checks. It does not measure final admission, selection, execution or trip
improvement. The retained timed-trip census has **51 blue-flag disagreements**,
while the hook accounts for **39**; 12 are outside this verdict coverage.
Census examinations and rollout verdicts count different events, so their
totals must not be equated generally.

`ArrivesAt` extrapolates a frame and uses `trap->EntityContact` against the goal.
That explains how an objective contact can become an arrival; it does not make
its query equivalent to the forecast's enclosing-box overlap or establish
final admission for every divergence.

## Decision: deprioritize repair; do not claim exhaustive closure

**Park the proposed item-branch repair.** There is no demonstrated completed-trip
payoff. The 39 conversions argue against investing in this measured subset,
not that the repair returns nothing everywhere. The non-flag medpak contacts
remain covered by the separate `bot_forecastItems=1` experiment, which
[showed no trip improvement at either class](2026-09-21-offer-limit-is-the-trigger-test.md).

- **`swept_not_endpoint` (60 without an initial conversion) is unresolved.**
  The diagnostic tests an enclosing axis-aligned box against brushes, not the
  hull's actual swept path. Some may be protective crossings; others may be
  box false positives. Keep conservative admission pending exact measurement.
- **`box_not_brush` (30 without an initial conversion) is a measured subset,**
  not the only genuine remainder or a count of safe final admissions. A repair
  needs its own CPU measurement and handling of moving triggers.

Speed work at classes 150 and 125 returns to a bounded candidate-generation
experiment: identify one recurring costly transition in retained traces and try
one variation under existing admission checks. Only new evidence of a concrete
payoff should restore trigger-branch priority. A renewed trigger study must
cover missing rollout paths and final candidate outcomes, and distinguish exact
swept contact from enclosing-box overlap. No speed win or complete bound on the
remaining opportunity follows from these counts.

## Binding

| Item | Digest |
| --- | --- |
| `codemp/game/bot2_forecast.c` | `b202ee0689307e02â€¦` |
| `codemp/game/bot2_forecast.h` | `9c2d725b1b21c912â€¦` |
| `codemp/game/bot2_move.c` | `a7a0cbc90f4c646aâ€¦` |
| `codemp/game/g_svcmds.c` | `999456707bfcd38aâ€¦` |
| `openjkded.x86.exe` | `ef7d69dcebc17e0câ€¦` |
| `jampgamex86.dll` | `8365513ee797e23fâ€¦` |

Source manifest `build/20260921T055552777704Z.sources.json` (1,117 files, all
unchanged since the build). `tools/test_movement_trigger_shadow.py` now passes
**34 cases**, including that a verdict is filed under the trigger that caused
the refusal rather than one examined later in the same call, that the
recoverable flag is cleared per rollout, and that the reporter is silent while
the diagnostic is off. The negative control still fails on the tunnelling case.

Records 186, dropped 0. Rollouts tagged 4,407, untagged 378. Only the straight
search reports an initial verdict; other paths contribute to the census but
not `judged`. The 129 judged rollouts and 186 disagreeing examinations describe
different populations, and neither is a final-admission count.

`bot_forecastTriggerShadow` remains **default 0**. The diagnostic is
decision-inert but not throughput-neutral. Its counts describe instrumented
search; even stable observed rates are not guaranteed for uninstrumented runs.
