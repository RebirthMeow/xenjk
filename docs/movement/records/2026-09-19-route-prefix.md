# The prefix: a recorded route, run from the benchmark stand

The follower measurement said what a route entry costs - open-loop playback
survives 28 units of along-track error and fails at 16 cross-track - and
argued for walking into the tape rather than steering it. That walk is built
here, and **all three human routes now run from the benchmark stand**, with
the tape unchanged and nothing steering it.

The stand is `(152,-1160,1924.125)`, which is where `red_out_L1_stand`
begins. The three routes complete the enemy-flag pickup there in **14,875,
15,350 and 19,000 ms**, walk included, against the search's **21,150 ms
median** for that case over 60 retained standard floors (best run 19,150).
The fastest is the route whose recording starts 392.8 units away:
the walk costs 2,000 ms and the tape then runs its recorded 12,875.

`bot_routePrefix` is default off. With it unset, playback is unchanged - the
control run for `1789686390-1-c0` reproduces 14,825 ms and 37.832 max drift,
the route-asset record's own figures.

## What was built

- `bot_routePrefix` arms a prefix run: the bot is placed at
  `bot_routePrefixStart` (the stand), the movement engine is given the tape's
  start as its objective, and this module submits the engine's commands until
  the bot is inside the entry gate and at rest. The tape's clock starts at the
  hand-off, so the route time is still the recorded route's and the walk's
  time is reported beside it, never inside it.
- The gate is measured on the axes the tolerance was measured on:
  `bot_routePrefixAlong`, `bot_routePrefixCross`, `bot_routePrefixVertical`
  and `bot_routePrefixSpeed`, with `bot_routePrefixSettle` holding the bot
  inside the gate before the tape starts. The route's first line comes from
  the tape's own marks - the first one more than 32 units from the start -
  not from the start yaw, which on a strafe run points somewhere else.
- `[ROUTEPLAY] PREFIX` reports the walk every 500 ms and `[ROUTEPLAY] HANDOFF`
  reports the state the tape inherits, once. `DONE` carries `prefix_ms` and
  the hand-off error, so a prefix run can never be read as a route time.
- `route_approve.py` gains `--prefix`, `--prefix-along`, `--prefix-cross`,
  `--prefix-speed`, `--prefix-settle`, `--prefix-spend` and
  `--prefix-timeout`, and records the hand-off and the walk's progress in
  `approval.json`.

## The planner gets close; it does not arrive

The first prefix run failed, and usefully. The engine walked 12.6 units in
half a second and then **stood still for the remaining 29.5 seconds, 23.1
units from the tape's start**, with no event and no decline. The walk's own hold detector never
fired: it names a hold only when the objective is more than 64 units away
(`x->held=dist<=1 && goalDist>64`), and this objective was 23 units off.

The cause is structural, not a bug: a planned path ends on a graph cell, the
walk drives to that cell's target, and **nothing in the engine closes the
distance between the last cell and a point that is not a node**. For a
flag objective that gap is covered by the pickup radius. For a route entry it
is the whole problem.

So the last cell is this module's business. Inside `bot_routePrefixClose`
(96 units) the prefix steers itself at the point; outside it, the engine
plans. That leaves the movement floor untouched, which is the reason for
doing it here.

## Three approach laws, two of which park the bot

Closing on a point turned out to be the entire difficulty, and each failure
was specific:

| law | what the bot did |
|---|---|
| `Bot2_MoveWaypointIntent`, radius 8, full throttle | stopped **13.3 units** short and held it for 30 s |
| proportional `(d-3)*4`, floor 60, wish-limited | arrived, but parked at **exactly 3.0 units** on one route |
| `sqrt(2*600*d)`, radius 0.5, floor 20 | **0.2 to 0.7 units**, at rest, on all three routes |

The first fails on quantisation: `MoveControl` picks a whole key combination,
and against a 20 units/sec intent the nearest legal command is *no command*,
so the bot stands. `Bot2_MoveControlLimited` with the wish speed as its limit
is what makes a slow command expressible at all.

The second fails on friction: a wish speed of 20 loses to ground friction, so
a proportional law creeps - one run spent **3.8 of its 4.35 seconds covering
its last nine units** - and the dead radius that fixes the creep becomes a
floor the bot cannot get under.

The third is the physical one. Ground friction below the stop speed removes
about 600 units/sec/sec, so `sqrt(2*600*d)` is the speed whose coasting
distance is exactly what is left: the bot runs, stops commanding, and drifts
onto the point. It is the only one of the three that arrives.

## The arm

Build `move-build-prefix-20260919-e`, world `route-prefix-20260919-e`, stand
`(152,-1160,1924.125)`, team red, gate `along 2, cross 2, speed 1, settle
100`, `bot_moveSpend 0`. Three repetitions of each route.

| route | prefix ms | route ms | total ms | recorded ms | outcome | max drift |
|---|---:|---:|---:|---:|---|---:|
| `1789686350-0-c0` | 2,000 | 12,875 | **14,875** | 12,875 | 3/3 pickup | 15.1-16.2 |
| `1789686390-1-c0` | 550 | 14,800 | **15,350** | 14,800 | 3/3 pickup | 19.1 |
| `1789686416-2-c0` | 450 | 18,550 | **19,000** | 18,550 | 3/3 pickup | 9.6-14.2 |

One run, verbatim:

```powershell
python tools/route_approve.py .codex-research/route-assets-20260918/1789686350-0-c0.route.json `
  --build .codex-research/move-build-prefix-20260919-e `
  --out .codex-research/route-prefix-20260919-e/1789686350-0-c0-r1 `
  --prefix --start "152,-1160,1924.125" --team red `
  --prefix-along 2 --prefix-cross 2 --prefix-speed 1 --prefix-settle 100
```

Every route time is its recorded time to the millisecond, every run reached
the enemy flag, no run died, and the hand-off landed within 0.7 units and at
a standstill in all nine.

For scale: played from their own starts with the prefix unset, the two tapes
controlled today run 14,825 and 18,575 ms, and the route-asset record has the
third at 12,900 ms. The bot's own search from this stand is 21,150 ms median
over 60 retained standard floors, ranging 19,150 to 36,200; the 19,150 ms
figure quoted elsewhere in this work is the best run of an 18-repetition arm,
not a median. See [where a route is worth taking](2026-09-19-route-selection.md).

## What this is and is not

- **These are stand times.** Unlike the route-asset record's numbers, the bot
  begins at the benchmark stand's exact coordinates and its own walk covers
  the difference. That is the thing that was missing.
- **The world is the route world, not the bench.** It runs `pmove_fixed 1`,
  `pmove_msec 8`, `pmove_float 1`, because the tape is a command sequence
  under one physics setting. The search figures were taken **without
  pmove_fixed**, so the comparison spans two physics settings and is
  indicative, not a matched arm.
- **It is not a level-1 fixture case.** `red_out_L1_stand` declares
  `force 0`; a prefix run starts the bot with the recording's pool of 100.
  Nothing spends it - the recordings show `active_powers_seen 0` and
  `force_decrease_seen false`, the walk runs with `bot_moveSpend 0`, and the
  hand-off reports `force=100/100` in every run - so no Force power is used
  by either half of the run, but the declared pool differs.
- **It is still open loop after the hand-off.** Nothing reacts to anything.
  Every run here is on an empty world with no other players.
- **One map, one stand, three routes.** The gate that works here is tighter
  than the measured tolerance on purpose; `1789686416-2-c0` failed at a
  hand-off of 5.5 units along and 4.7 units/sec, inside the tolerance the
  follower record measured for a *different* route. Entry tolerance is
  per-route, as that record warned.
- **The search is not improved by any of this.** The deficit measured in
  [where the stand time goes](2026-09-18-where-the-stand-time-goes.md) is
  untouched. What is shown is that a recorded line, entered by the bot's own
  walk, beats the search from the same stand. It does not beat it from every
  start: from the two red outbound respawns the same route loses by 6.8 and
  17.1 seconds, because its start is behind them
  ([where a route is worth taking](2026-09-19-route-selection.md)).

## Verification

- Controls with `bot_routePrefix` unset: `1789686390-1-c0` reproduces
  **14,825 ms, max drift 37.832**, and `1789686416-2-c0` **18,575 ms, max
  drift 23.791** - the route-asset record's figures, unchanged.
- 94 movement tool tests pass.
- The only engine file today's work touches is `bot2_route_play.c`, which
  nothing but a loaded tape calls, so the benchmark floor cannot be affected
  and was not re-run.
- Nine prefix runs, three per route, all approved by `route_approve.py`'s own
  verdict: pickup reached, drift inside the 64-unit limit at every mark.
