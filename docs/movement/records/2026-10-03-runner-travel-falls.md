# B3: the runners' travel falls (2026-10-03)

Owner (2026-10-03): accept camping's fall cost ([camp falls](2026-10-03-runner-camp-falls.md)), then work on the
runners' travel falls in general. Offline first on the retained 4v4 worlds
(`.codex-research/b3-20261002-a/runcamp/`, 32 worlds, build `b3-build-20261002-j`; camping does not change travel, so
both arms count), then one change measured.

## What the 301 runner falls are

`falls.py` classes (both runners, 9.4 a world):

| class | falls | a world | what |
|---|---|---|---|
| thrown | 114 | 3.6 | a defender `THROW`/`HIT` matched to the runner in the 3 s before; 90 of them carrying |
| credited | 77 | 2.4 | an enemy credited by the game, no throw matched: shot or splash knock mid-hop (53 show `knock:hit`) |
| own | 110 | 3.4 | no enemy credited, no throw: the runner's own hop sequence, last edge a hop launch in 87 |

Thrown and credited falls cluster just outside each enemy stand (the carrier leaving with the flag) and on the
central walkway. Defender pulls hit an airborne target 2,107 times against 604 grounded (all `HIT` lines): the
native counter (`CanCounterThrow`) needs the target on the ground with its own push or pull usable, and moving costs
it a level, so a hopping runner never counters. That is a speed-for-safety trade, not a defect; changing it is a
policy question for the owner.

## Own falls

Per hop (`hops.py`, 21,560 runner launches above the pit floor; a fall is the next grounded sample in the pit), quiet
hops - no enemy within 600 u at launch, nobody within 96 u in flight - fall about 1% a launch with no hot spot on the
map (`hops.json`). Launches at 200-300 u/s fall 3.7%, faster ones under 1%.

A body in the flight roughly doubles the fall chance (`contact.py`: another player's interpolated position within
40 u horizontally and 64 u vertically, -250..+1,000 ms of the launch): 3.4% of hops with contact fall against 1.8%
without; 39 of the 410 fall-ending launches had one, teammates and enemies alike (chasers and campers most).

Named case: `on-1`, blue runner, level 318,575. A reroute committed one hop landing at (2030, 4102) on the walkway;
the blue chase D (`Chaser2_v2`, a teammate) flew head-on into the launch (positions 45 u apart; its velocity
488,359 -> 69,-120, the runner's -342,-361 -> -32,-396). The runner flew on the deflected line into the pit with no
miss and no recovery. The hit test in `Move_Command` compares the velocity with what the last Pmove left; a body
clips the velocity inside Pmove, so it never fires, and no rollout models bodies. The touch was already recorded
(`contact.transient`, for failure attribution) but never voided the sequence.

## The change

`bot_moveContactKnock` (measured at default 0; default on since, owner): a Pmove that touched another client and left the bot airborne, during a committed
hop (stage before onward), voids the sequence as a hit does, and the knock recovery steers from where the body left
it. A `[MOVE] ... edge=knock reason=contact` line is logged in both settings, so arms compare
(`.codex-research/contact-20261003-a/contacts.py`). It applies to every movement bot, defenders included.

## Measured

Build `contact-build-20261003-a`, `.codex-research/contact-20261003-a/` (`run.sh`), 16 worlds an arm, 4v4 as in
`runcamp`, primary `runners_falls`.

Per 600 game s (`laps_compare.py`; one `on` world replayed another, 15 distinct):

| | off | on | difference |
|---|---|---|---|
| falls, both runners (`runners_falls`) | 11.11 | 8.76 | **-2.34 +- 1.02** |
| camper falls (blue camper) | 5.86 | 4.26 | -1.61 +- 0.76 |
| weapon deaths, both runners | 29.26 | 30.24 | +0.98 +- 1.26 |
| defender deaths (all but self-kill) | 22.28 | 25.18 | +2.90 +- 0.83 |
| captures, both teams | 7.22 | 7.96 | +0.74 +- 0.71 |
| pickups, runner team | 29.50 | 29.75 | +0.25 +- 1.16 |

Attributed by the contact lines (falls a world with a body contact logged in the 8 s before the death, every role):

| role | off: after contact / other | on: after contact / other |
|---|---|---|
| runners | 2.69 / 8.56 | **0.94** / 7.94 |
| campers | 1.88 / 10.19 | **0.50** / 8.50 |
| chase D | 1.62 / 3.56 | **0.62** / 3.50 |
| base D | 0.06 / 3.19 | 0 / 3.19 |

The falls that follow a body contact drop by about two thirds for every role; the others do not move beyond noise.
Runners touch another body in flight about 49 times a world (off). The defenders' extra deaths are weapon deaths
(saber +1.7, pistol and rocket up), while their falls drop (6.3 -> 5.1 a world): attackers that stop falling live to
fight. Captures and pickups do not drop.

**Default (owner, 2026-10-03):** `bot_moveContactKnock 1`. Remaining falls: thrown (3.6 a world, the airborne runner
cannot counter a pull) and credited knocks (2.4); owner: the runner does not hop less near pits to counter them; quiet own falls (about
1% a launch, no hot spot) need in-play parity to place.
