# Milestone B1: the camper's displacement throw (2026-10-02)

Plan: [MILESTONE_B.md](../MILESTONE_B.md), B1 open item "pushing a respawning base away". Build
`displace-build-20261002-a` (base c88a6fb7 plus this change), `.codex-research/displace-20261002-a/` (`run.sh`,
`displace_summ.py`, `summary.txt`). Control: `camper-20261002-b/ce` (build `camper-build-20261002-e`, same sources
without the change). Kejim laps worlds of ten game minutes with the milestone A defenders and the camper, 8 worlds an
arm, one at a time; every world distinct, no crashes.

## What was built

Community rule for the non-carrying offense: "cut the enemy base off from its carrier (push a respawning base away)".
One-sided there is no enemy carrier, so the camper keeps an enemy defender off its own runner instead.
`bot_camperDisplace 1` (default 0; `Displace` in bot2_defender.c, called from `Engage` when no lethal throw is on):

- When: the own runner is carrying, or within `bot_camperDisplaceGrab` (1,500) of the enemy stand; the visible
  target (base D or chase D) is within `bot_camperDisplaceNear` (800) of the runner and in the throw's reach.
- What: pull or push, modelled as the lethal throw is (`ThrowVelocity` with the target's `CounterLevel`, `Sweep`
  for the flight). Gain = how much farther from the runner (horizontally) the target lands than its own velocity
  would have carried it over the same time (at most 1 s), so a countered throw that only stops a closing target
  counts too. The kind with the larger gain is thrown if the gain is at least `bot_camperDisplaceGain` (200).
  A flight the sweep calls lethal is accepted (it is the largest displacement).
- Force: `bot_camperDisplaceReserve` (0) kept beyond the throw's cost; at most one every `bot_camperDisplaceMs`
  (1,000).
- Logs: THROW `reason=displace`, a DISPLACE line (gain, runner distance, carrying, counter, grounded, Force), and
  DISPLACE_RESULT (landed / miss / pit / death / survived, horizontal distance moved, distance from the runner
  before and after). A landed displacement ends the follow, so a lethal throw need not wait out the 4 s outcome.

## Results (per ten-minute world)

| | control (`ce`) | displace (`dp`) |
|---|---|---|
| runner-team captures (se) | 10.75 (1.29) | 9.38 (1.44) |
| runner / camper captures | 7.38 / 3.38 | 5.38 / 4.00 |
| runner pickups | 17.0 | 15.0 |
| runner deaths: weapon / falling | 12.0 / 2.4 | 13.4 / 2.2 |
| camper deaths (falling) | 18.4 (4.4) | 16.1 (2.6) |
| defender deaths by the camper (base D / chase D) | 6.1 / 3.4 | 5.6 / 3.2 |
| camper lethal throws | 1.00 | 0.88 |
| displacement throws | - | 10.5 (2-18), pull 56% |
| displacement results | - | landed 7.75, miss 2.12, pit 0.62 (hit 80%) |
| target moved (hits) | - | mean 174 u, median 131 |
| target's distance from the runner, after minus before | - | mean +229 u, median +136 |

- The throw works mechanically: ten a world, 80% hit, the target lands on average 230 u farther from the runner,
  and 0.6 a world fall into the pit (a defender death the lethal test did not take because air input escaped).
- **Every throw was at a target that could not counter** (counter 0 in 84 of 84; 51% grounded but firing or with
  the hand busy). A grounded idle defender has push/pull 3 and counters fully; the gain test refuses those throws,
  since a countered throw moves it about 16 u.
- **No capture benefit**: runner-team captures 9.38 against 10.75 and runner captures 5.38 against 7.38 are within
  the arms' noise (about 1.4 se each; the runner difference is 1.5 combined se). Nothing factor-sized either way.

## Verdict

Default 0 (off): built and mechanically sound, but 8 worlds show no capture gain, and the point estimate for the
runner's own captures is lower. Pushing a defender 150-250 u does not keep it out of weapon range (runner weapon
deaths did not fall), which is what kills the runner here.

## Open

- A displacement only matters if it breaks the defender's line of fire or route; a gain measured in distance from
  the runner does not test that. Candidate: score by breaking sight to the runner, or only into a hazard.
- Not run: a carrying-only variant (`bot_camperDisplaceGrab 0`), a Force reserve for the camper's own lethal throw.
