# Where a route is worth taking

The prefix makes a recorded route usable from a start that is not its own.
The next question is which starts, and the answer is **narrow and
measurable**: of the three red outbound level-1 fixture cases, the route wins
at one and loses badly at the other two, and the reason is not the route -
it is the cost of reaching a point on it that the bot can enter.

This record adds no code. It is three prefix runs against the retained
search population, plus what the asset itself says about the alternative.

## The comparison

Route `1789686350-0-c0`, the fastest demonstration, entered at its own start
by the prefix, against the search's own time for the same fixture case. The
search figures are every red outbound level-1 case in **60 retained
standard-configuration floors** (`rewrite`, `edges`, full 36-case runs).

| case | search median | search best | prefix walk | route | total | verdict |
|---|---:|---:|---:|---:|---:|---|
| `red_out_L1_stand` | 21,150 | 19,150 | 2,000 | 12,875 | **14,875** | route by 6,275 |
| `red_out_L1_respawn4` | 18,325 | 16,700 | 12,275 | 12,875 | **25,150** | search by 6,825 |
| `red_out_L1_respawn5` | 16,375 | 13,700 | 20,625 | 12,875 | **33,500** | search by 17,125 |

Every run reached the enemy flag; the losses are not failures, they are the
walk. The two respawns are 2,593 and 3,543 units from the route's start, and
that start is **behind them**: the recording begins near the red stand at
y = -832 and the respawns sit at y = 1,696 and y = 2,624, already a third
and a half of the way to the enemy flag. Entering at the start means walking
back down the map and running the whole route again.

**A correction to the baseline this work has been quoting.** The 19,150 ms
figure from [route assets](2026-09-18-route-assets.md) is the best stand run
in an 18-repetition arm. Across the 60 retained floors the stand's median is
**21,150 ms**, ranging 19,150 to 36,200, and `red_out_L1_stand` is the
slowest of the three cases, not a typical one. The prefix's margin at the
stand is therefore larger than the first record claimed, and the respawns
were never the 19,150 ms problem.

## A suffix entry does not rescue them

The obvious repair is to join the tape part-way - take the route from its
nearest point rather than its start. The asset says that will not work here
either:

| start | nearest contract mark | distance off the line | recorded speed there | route left |
|---|---|---:|---:|---:|
| `stand` | 4, at 2,000 ms | 31.0 | 313.9 | 10,875 ms |
| `respawn4` | 22, at 6,500 ms | 1,358.0 | 727.7 | 6,375 ms |
| `respawn5` | 26, at 7,500 ms | 1,598.2 | 794.2 | 5,375 ms |

Two things have to be true at a join and neither is. The bot has to **get
there** - 1,358 and 1,598 units is most of the walk it was trying to avoid -
and it has to **arrive in the recorded state**, which at those marks is 727
and 794 units/sec of flight. A walk tops out at the 250 units/sec wishspeed
ceiling, so those states are not ones the bot can be delivered into at all.
This is the same chain-breakage wall the search work measured: a live speed
of 250 against an entry speed of 400 is a walk that cannot enter a flight,
and 794 is further out of reach than 400.

Joins that a walk can deliver are the marks whose recorded speed is low, and
on this route that is only the first one. **A route recorded from a standing
start is enterable at its start and nowhere else.**

## The selection rule this implies

A route is worth taking when

    cost(reach an enterable mark) + route time after that mark  <  search time

and the enterable marks are those whose recorded state the bot can actually
arrive in - in practice, the ones at or near rest. For the three assets in
hand that reduces to a single usable entry each, at the recording's own
start, which makes selection a question about **where recordings are taken**
rather than about a runtime choice between many routes:

- a recording that starts at a fixture start makes that case fast, and no
  other case;
- to cover the respawn cases, the useful thing is a recording *from those
  respawns*, not a cleverer way to enter this one.

That is a request to the owner, not a piece of engine work: three more
demonstrations, started at `(-640,1696,1808.125)` and
`(-320,2624,1368.125)`, would let this measurement be repeated where the
route currently loses.

## What the format would need, if a suffix entry is ever wanted

Worth recording because the follower record's note is half right. The asset's
contract already carries **`velocity` and `yaw` per mark**, so a mid-route
entry state exists in the asset today. It is the serialised tape that drops
it: `route_asset_build.tape_text` writes `k <ms> <x> <y> <z> <speed> <kind>`
and the engine parses exactly that. A suffix entry therefore needs a tape
field and a playback that starts at command index k, **not** a new recording
regime or a denser contract. Nothing here asks for that work; it is the
measurement above that says when it would be worth it.

## Limits

- **One route, one map, one leg.** All three assets are the red outbound leg
  and all three start within 400 units of the same stand.
- The search figures are `time_ms` from retained floors taken **without
  `pmove_fixed`**, while a prefix run sets it; the comparison spans two
  physics settings, as the [prefix record](2026-09-19-route-prefix.md)
  states.
- The prefix totals are three repetitions at the stand and three at each
  respawn, and the table quotes the median walk. The walk repeats closely:
  identical to the millisecond at the stand (2,000 ms three times), and
  12,275/12,175/12,375 and 20,625/20,775/20,600 at the two respawns - a
  spread of 200 ms against margins of 6 to 17 seconds. All nine runs reached
  the enemy flag, so the respawn losses are the walk's cost, not a failure of
  the tape.
- Every run is on an empty world. Contention is untested.
