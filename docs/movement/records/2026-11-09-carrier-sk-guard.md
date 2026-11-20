# 2026-11-09: a carrier declines a self-kill request near a capture (`bot_carrierSkHomeMs`, review BA-1)

Branch `claude/bot2-review-next`, 5f4bc099. Review item BA-1 in [docs/review/2026-11-09-bot2.md](../../review/2026-11-09-bot2.md)
(lane report [ai.md](../../review/2026-11-09/bot2/ai.md)).

## What was wrong

`RunnerCamp` asks its team's carrier to `/kill` when the asking runner is within 600 u of the enemy stand with
health 80+ and Force 75+, and the carrier has health under 40, or Force under 20 with Speed down. Neither the request
nor the honour (the runner's `SK_HONOUR`, the camper's `CAMPER_SK`) looks at where the carrier is, and Force under
20 with Speed down is the normal state late in any carry. A carrier a few seconds from a capture could kill itself;
the flag then goes back to the enemy stand for a full carry.

Sized first from the retained controls (replan-budget-20261109-a: 34 Kejim and 18 Dantooine default worlds, 33 and
16 distinct): `CAMPER_SK` 8 times on Kejim (0.24 a world) and 5 on Dantooine (0.28), `SK_HONOUR` never (in the 4v4
the requester is always the runner and the carrier the camper). Euclidean distance from the camper's own stand at
the self-kill: Kejim 23, 1,044, 1,240, 1,417, 2,690, 3,936, 4,760 u; Dantooine 31, 277, 837, 1,545, 1,662 u. The ones
at 23-277 u are at the stand: a capture there was impossible, so the own flag was away (a standoff), and the
self-kill is what the design wants. Whether a capture was possible is not in the log.

## The change

`bot_carrierSkHomeMs` (default 0, off). With N > 0 a carrier declines a standing request while its own flag is on
its stand (reaching the stand captures) and its route home is shorter than N ms: the shared graph's route cost, as
the defenders' `RouteMs`, else 250 u/s; checked at most every 500 ms a carrier. In a standoff the request stands as
before. One helper (`Bot2_SelfKillHonour`) serves both honour sites; `[RUNNER] SK_DECLINE ... route_ms= guard_ms=`
logs a decline, at most every 2 s a carrier. The requester keeps asking (its rule is unchanged).

## Expected effect (written before measuring)

The arm uses N = 5,000 ("a few seconds from a capture"). Declines are rare: at most the share of the 0.24-0.28
self-kills a world that happen with a capture possible and within 5 s of home, likely under 0.1 a world. Team
captures cannot move by a decidable amount at 16-24 worlds; what the pair can show is the declines themselves
(how many, where) and whether the declining carrier captured.

## Measurement

Build `.codex-research/review-next-build-20261109-b` (5f4bc099). Evidence `.codex-research/baarms-20261109-a/`
(`runarms.py`, `run.sh`, `chain.log`, `worlds/`, `compare-*.txt`). Arm `sk` (`bot_carrierSkHomeMs=5000`), Kejim seeds
35-50 and Dantooine 35-42, the milestone-B 4v4 at rate 25 with the frame meter, paired by seed against the guide
campaign's control worlds (`guidelong-20261109-a/worlds/ctl-*`, build -a, the reused control; this build's defaults
must replay it on seed 35 of each map first).

## Measured (2026-11-09)

The build's defaults replay the reused control on seed 35 of each map (game log and 156,331 / 142,444 decision
lines identical; `identity.txt`). Then all 24 `sk` worlds replay their control worlds exactly (16 of 16 Kejim, 8 of
8 Dantooine): `SK_DECLINE` never printed. The 24 worlds hold 6 camper self-kills (5 Kejim, 1 Dantooine); none came
with a capture possible inside 5 s of home. Team captures therefore read 0.00 on both maps (`compare2-*.txt`,
against the campaign's control only; the first `compare-*.txt` counted the identity world twice, because a build
string makes its game log hash differ).

So the gap BA-1 names is real in the code and absent from these worlds: at 0.24-0.28 self-kills a world, and none
of them near a possible capture, a pair cannot measure it. The guard is cheap and changes nothing where it does not
fire. Owner's call: leave it at 0, or turn it on as a correctness guard (5,000 ms) with no measured effect.

## Default (owner, 2026-11-10)

`bot_carrierSkHomeMs` is 5000 by default since 1fb110d0 (owner: "Default on at 5000 ms"), as a correctness guard;
0 turns it off. No measured effect: it fired in none of the 24 paired worlds.
