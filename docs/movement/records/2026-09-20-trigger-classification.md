# The trigger contacts are falls, and the gate is right

> **Superseded analysis, review correction 2026-09-20.** See
> [review corrections](2026-09-20-opus-review-fixes.md).
>
> The observed hurt-volume contacts support retaining the safety gate. They
> do not prove that every possible speed-preserving continuation is lethal or
> that only one optimization lever remains. Follow-up budget owner counts and
> counterfactual bounds were corrected; treat broad causal conclusions below as
> hypotheses limited to the sampled diagnostic.

Recorded 2026-09-20. One diagnostic world, no source change, no rebuild, the
frozen reference build. Evidence in `.codex-research/trigger-class-20260920-a/`.
Follows [the leg budget](2026-09-20-leg-budget.md), which found that 748 of the
1,029 candidates judged during the leg's most expensive grounded frames were
refused on unsupported bit 256 and that none of them had been classified.

## Method

`bot_forecastTriggerLog` already exists in
[bot2_forecast.c](../../../codemp/game/bot2_forecast.c) and prints the touched
entity, its classname, the contact position and the trigger's bounds. It is
default off. Nothing was written for this work: the reference command was run
once with `--set bot_forecastTriggerLog=1` added.

```powershell
python tools/movement_bench.py `
  --build .codex-research/successor-aim-build-20260920-a `
  --out .codex-research/trigger-class-20260920-a/run-1 `
  --filter '^red_out_L1_stand$' --edges --parity 256 --parallel 1 --port 32411 `
  --graph .codex-research/move-explore-20260916-b/seed.txt --momentum-trace `
  --set bot_moveReplay=0 --set bot_moveHeadingSweep=0 --set bot_moveStatePrefer=1 `
  --set bot_moveReroute=1 --set bot_moveExitSpeed=2000 --set bot_moveSuccessorAim=1 `
  --set bot_forecastTriggerLog=1
```

Pass, 19,050 ms, zero parity mismatches, movement owner only.

**The instrumentation did not perturb the run.** The world's momentum trace is
identical to retained world `confirm-1-r8` - the same 761 samples and the same
`trace_sha256`, `1e6f0ae1f7b56e0b91d3a833...` - and its leg budget matches on
every figure: 19,025 ms, mean 381.2 units/sec, 97 waiting and 34 run-up frames.
`TriggerLog` prints from inside the rollout, which is the shape
[LESSONS.md](../LESSONS.md) records as having cost 2,000 ms once, and here 547
such lines cost nothing at all. That is one run on one arm and does not
overturn the empirical rule, but it is evidence that printing inside the
rollout is not by itself the mechanism.

## What is actually being touched

547 contacts, every one classified:

| Entity | Contacts | Distinct entities | When, relative to the leg |
| --- | ---: | ---: | --- |
| `trigger_hurt` | 288 | 1 | throughout, +2,300 to +14,700 in leg |
| `team_CTF_blueflag` | 172 | 1 | +15,625 to +19,025 only |
| `team_CTF_redflag` | 53 | 1 | +25 to +225 only |
| `item_medpak_instant` | 33 | 2 | two sites, +2,300 and +12,800 |
| `trigger_push` | 1 | 1 | once |

Pooled over every multi-frame grounded run - the expensive ones the leg budget
isolated - the contacts are **`trigger_hurt` 221, `item_medpak_instant` 33,
`team_CTF_blueflag` 47, `trigger_push` 1**, and every one of the blue-flag
contacts falls in the final approach from +15,625 onward.

Per grounded run, in order:

| Landing | Frames | Speed | Contacts |
| --- | ---: | --- | --- |
| +2,325 | 26 | 496.8 -> 254.4 | hurt 39, medpak 12 |
| +6,250 | 4 | 548.3 -> 256.5 | hurt 12 |
| +7,150 | 29 | 389.6 -> 253.6 | hurt 96, push 1 |
| +12,825 | 16 | 624.3 -> 253.7 | medpak 21, hurt 19 |
| +14,025 | 32 | 388.1 -> 250.0 | hurt 43 |
| +15,625 | 5 | 387.8 -> 250.7 | hurt 12, blueflag 5 |
| +17,725 | 5 | 412.8 -> 257.9 | blueflag 26 |
| +18,650 | 14 | 390.0 -> 134.9 | blueflag 16 |

## The finding

**The refusals are correct and there is no recoverable pool.** Through the
productive middle of the leg the refused candidates are flights into the map's
single hurt volume. The bot brakes at those landings because every continuation
that keeps the speed kills it. The 24-unit run-up is not a bookkeeping defect;
it is a deliberate brake in front of a hazard.

The two classes that are not falls are both outside the problem:

- `team_CTF_redflag` at +25 to +225 is the stand start inside the bot's own
  flag box, which `StraightSearch` already reports as `deferred` and defers to
  the cadence.
- `team_CTF_blueflag` from +15,625 is the objective in the final approach.
  Eight rollouts convert to `arrives`; the rest are near-misses at the end of a
  leg that is about to finish anyway.

`item_medpak_instant` is 33 contacts at two sites. `bot_forecastItems`, which
already exists and passes `IT_HEALTH`, would recover exactly those and nothing
else, so it is not worth a campaign on this evidence. The one place it might
matter is the +12,825 landing, where the bot arrives at **624.3 units/sec** -
the highest speed recorded anywhere in this work - and medpak contacts (21)
outnumber hurt contacts (19) before it brakes to 253.7. That is a single-site
hypothesis worth one pilot, not a general admission change.

## Why this cannot be fixed in the aim vocabulary

`d0` sets the hop's heading and nothing else. `StraightSearch` computes
`d0 = (0.695 * v0 + 76) * aim` and then calls
`AimAt(ps->origin, aim0, ref, candidate->hop[0].dir)`, which normalises to a
direction. How far the hop travels is set by the physics - a tap is 225 of
vertical speed against gravity 800, and the horizontal reach is whatever the
speed carries through that airtime - not by where the aim point sits.

So at a 545 units/sec landing in front of the hurt volume, all six tap-now
candidates point within a few degrees of each other down the same straight,
all fly the same distance, and all land in the same pit. That is exactly what
the candidate log shows: every `r0=0` candidate refused with `land0=-1` and
`done=-1`, every admitted candidate carrying `r0=24`. Adding shorter aim
fractions would change the heading, not the reach, and would not help.

**The lever is not the hop.** Where it is was left open here and is settled
in [the route pricing record](2026-09-20-route-pricing.md), which corrects an
over-broad reading of this section: the live planner is `Graph_PlanState` and
it already prices links from the speed actually carried. The walk model
survives only in the reroute gate.

## What this retires and what it opens

Retired:

- **Classifying the trigger contacts.** Done. They are falls. No admission
  exception is warranted and none should be proposed on this evidence.
- **Enabling `bot_forecastItems` as a general remedy.** Worth 33 of 288 in-leg
  contacts at two sites.
- **Shorter first-hop aim fractions.** `d0` is a heading, not a reach.

Opened:

- Price the route by what it costs to fly, not to walk - narrowed to the
  reroute gate alone by [the route pricing record](2026-09-20-route-pricing.md).
- The single-site `bot_forecastItems` question at the +12,825 landing, where
  the bot gives up 370 units/sec from the fastest state recorded.

Lowered:

- The in-search-work coupling (handoff experiment 4). 547 prints from inside
  the rollout reproduced a retained trace exactly. With the `va` mechanism
  already excluded, the 2,000 ms incident looks specific to that patch rather
  than to in-search work in general.
