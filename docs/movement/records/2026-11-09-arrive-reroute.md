# 2026-11-09: the arrival safety check rolls a rerouted arrival as flown (`bot_moveArriveReroute`, review BX-1)

Branch `claude/bot2-exec-deep-bab45d`, ae6fbe2f (on the BM-3 commits). Review item BX-1 in
`.codex-research/review-bot2-20261109/exec-deep.md`. Owner, 2026-11-09: "Yes, fix BX-1", behind a cvar, default 0.

## What was wrong

`bot_moveArriveSafe` rolls an arrival on past the flag to see whether it ends standing, through `StraightCommand`,
which ends a rollout at the first landing off the path unless `straightPathFree` is set. The reroute pass set
`straightPathFree` (and, for its second roll, the replacement route in `rolloutPathOverride`) for its own rollouts and
cleared both on the line before `StraightArrives`. A candidate is in the reroute pass because one of its landings is
off the path, and that landing comes before the pickup; so the check's continuation stopped issuing the plan at that
landing and coasted there. The second hop, or the onward walk through the flag, was never judged. About a fifth of
arrival commits are rerouted (6.4 a world).

## The change

`bot_moveArriveReroute` (default 0). With 1 the reroute pass's two calls to `StraightArrives` and the cached take's
validation (`SearchCacheValidate`) keep `straightPathFree` and the replacement route set across the arrival check,
then clear them. A refusal on that path prints one extra `[ARRIVE] ... rerouted_unsafe=1` line (only with the cvar
on). Off, the code path and every line are today's.

## Measurement

Build `.codex-research/arrivereroute-build-20261109-a` (ae6fbe2f). Campaign `.codex-research/arrivereroute-20261109-a/`
(`runarms.py`, `run.sh`, `work.py`, `analyse.sh`, `identity.txt`, `work.txt`, `compare-*.txt`, `arrive-after-*.txt`):
the milestone-B 4v4, 600 game s at rate 25, frame meter on, as the BM-3 pair ran it. Control reused (PLAN.md, cheap
comparisons, rule 3): the 40 `ctl` worlds of `chargeflown-20261109-b` (build 1d2e7dea, the same defaults). Check
first: the defaults on this build replay that control on Kejim 3-4 and Dantooine 3-4 (game logs identical but the
build string, every decision count identical to 600,000 ms; `identity.txt`). Then `ar` (`bot_moveArriveReroute=1`),
seeds 3-22 on both maps, paired by seed with the control. 45 worlds, no crash, no late setup command, every game log
distinct.

**Arrivals** (`work.txt`, to 600,000 ms, a world, ar minus ctl paired, +- one standard error):

| | Kejim ctl | ar - ctl | Dantooine ctl | ar - ctl |
| --- | --- | --- | --- | --- |
| rerouted arrivals refused by the check that the old check admitted (`rerouted_unsafe`) | (not printed) | 0.8 +- 0.3 | (not printed) | 1.2 +- 0.8 |
| arrival candidates refused unsafe, all | 14.6 | -1.2 +- 1.4 | 22.4 | +4.9 +- 1.6 |
| arrival commits | 33.9 | -1.4 +- 0.9 | 28.9 | +1.5 +- 0.6 |
| ... rerouted | 7.5 | +0.5 +- 0.4 | 4.8 | +0.1 +- 0.5 |
| ... rerouted, flag taken in the air on hop 2 | 1.2 | -0.1 +- 0.2 | 0.2 | +0.0 +- 0.0 |

The fixed check refuses about one rerouted arrival candidate a world that the old one admitted (counted per candidate
judged, not per commit; worlds diverge after the first one, so the other rows are not attributable to it). World
deaths within 6 s of a rerouted commit with the flag taken mid-air on hop 2 (`arrive-after-*.txt`): Kejim 2 of 26
(ctl) against 1 of 22 (ar), Dantooine 0 of 5 against 0 of 5. Too few to read.

**Outcomes** (`tools/laps_compare.py`, a world, ar minus ctl, Welch se):

| | Kejim ctl | ar - ctl | Dantooine ctl | ar - ctl |
| --- | --- | --- | --- | --- |
| team captures (primary) | 3.69 | -0.12 +- 0.29 | 4.90 | -0.18 +- 0.49 |
| all captures | 7.39 | -0.20 +- 0.58 | 10.00 | -0.50 +- 0.96 |
| pickups | 29.24 | +0.09 +- 0.79 | 26.46 | +0.07 +- 0.64 |
| runner falls | 4.73 | -0.49 +- 0.70 | 1.33 | -0.15 +- 0.28 |
| falls of both runners | 8.92 | -0.30 +- 0.82 | 2.91 | -0.00 +- 0.42 |
| weapon deaths of both runners | 31.68 | -0.31 +- 1.02 | 32.37 | +1.29 +- 1.20 |

Verdict on team captures: **undecided on both maps**; no metric past the cut-off. Work (steps, expansions, estimated
frames over 25 ms, hops, replans): no change outside one standard error on either map.

## Reading

The fix does what it should (the check now refuses the rerouted arrivals whose real continuation is unsafe, about one
candidate a world) and costs nothing measurable. The fall it was meant to prevent is too rare in these worlds (a
rerouted mid-air pickup is about one commit a world) for 20 seeds to show it. As a correctness fix it is the owner's
call; default left 0 as instructed. Not installed.
