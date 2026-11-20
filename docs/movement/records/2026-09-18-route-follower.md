# The follower, and what open-loop playback already tolerates

The follower was meant to make a route asset survive a start that is not its
own. It does not yet, and the more useful result is the measurement taken on
the way: **open-loop playback already tolerates a large along-track entry
error and a small cross-track one**, which says what a prefix has to deliver
and whether a follower is needed at all.

`bot_routeFollow` and `bot_routeFollowGain` are default off and **not
qualified**. With them unset, playback is byte-for-byte what it was.

## What was built

- `bot_routeFollow` arms the follower: the bot is placed at
  `bot_routeFollowStart` rather than the tape's start, grounded commands are
  rotated toward the recorded line, and a run that drifts past
  `ROUTEFOLLOW_ABORT` stops instead of playing the tape into geometry it was
  not recorded in.
- `bot_routeFollowGain` is the rotation per unit off the line. **Zero arms the
  off-start placement and the abort with no steering at all**, which is what
  made the tolerance measurement below possible.
- `route_approve.py` gains `--follow`, `--gain` and `--start`.

The rotation turns the whole recorded frame, never the aim: a strafe run's
view yaw is deliberately offset from its velocity and that offset is what
accelerates it, so aiming at the line would throw the run away to correct it.

## Open-loop entry tolerance, with no steering

Route `1789686390-1-c0`, gain 0, one axis at a time from the tape's start.
Over its first 475 ms the recording moves (+50.3, +18.9), so **X offsets are
mostly along-track and Y offsets mostly cross-track**.

| offset | outcome | elapsed ms | max drift |
|---|---|---:|---:|
| x-24 | approved | 14,825 | 54.3 |
| x-16 | approved | 14,825 | 48.1 |
| none | approved | 14,825 | 37.8 |
| x+8 | approved | 14,825 | 34.4 |
| x+16 | approved | 14,825 | 34.2 |
| x+24 | approved | 14,825 | 41.4 |
| x+28 | approved | 14,825 | 45.1 |
| x+32 | **diverged** | 6,000 | 311.3 |
| y+16 | **diverged** | 2,000 | 325.5 |
| y+24 | **diverged** | 5,500 | 281.2 |

**Along-track error is cheap and cross-track error is not.** At least 28 units
either way along the line still completes in the recorded time to the
millisecond; 16 units across it does not survive two seconds. Starting ahead
or behind on the same line is a phase offset the tape absorbs; starting on a
parallel line puts the recorded takeoffs at the wrong ledges.

Gain 0 at zero offset reproduces open-loop playback exactly - 14,825 ms and
37.832 max drift, the same figures as the approval run - so the follower's
own code path is neutral when it is not steering, and the table above is a
measurement of playback, not of the follower.

## The follower itself does not work yet

Gain 0.30, same route:

| start | open loop | with steering |
|---|---|---|
| tape's own | approved, 37.8 drift | **tape_end, 78.3** |
| y+16 | diverged at 2,000 ms, 325.5 | tape_end, **95.1** |
| y+24 | diverged at 5,500 ms, 281.2 | diverged at 6,000 ms, 361.5 |
| y+32 | not run | tape_end, 103.2 |

Three things at once. It **hurts on the line**: a bot with no error is steered
to twice playback's drift. It **helps off the line**: y+16 goes from blowing up
in two seconds at 326 units to running the whole tape at 95. And it is
**non-monotonic**: y+32 completes where y+24 diverges. No off-line start
reaches the flag, so nothing here is usable.

## Why, and what it costs to fix

The error signal is wrong, and the asset cannot currently make it right.

Cross-track is measured against the chord between two contract marks, so the
recording's own curvature reads as error: with the bot exactly on the line the
follower still commanded 0.86 and -1.07 degrees before the first takeoff, and
on a route that is airborne from 500 ms onward those two commands are most of
the authority it has.

Interpolating the reference point along the chord by time **does not fix it,
and cannot**: the interpolated point lies on the chord, so subtracting it
changes only the along-track component and leaves the perpendicular distance
identical. The retry reproduced 78.283 exactly, which is the arithmetic, not a
coincidence.

A correct error signal needs the recording's true position at the current
instant, which is not on the chord. The contract stores a mark every 500 ms
plus takeoffs and landings; the recording behind it has one row per Pmove.
**The contract was designed to check a replay, not to steer one.** The same
gap shows in the serialised `k` line, which keeps scalar speed and drops the
velocity vector, so a follower cannot match a heading either.

Two ways forward, and they are not equal:

- **Densify the contract** - emit a position and velocity per command, or per
  25 ms - and retry the follower. This is an asset format change and the
  follower is still unproven after it.
- **Build the prefix instead.** The walk only has to deliver the bot inside
  the measured tolerance, and the tolerance in the sensitive direction is
  about 16 units, which is well inside what the existing planner can hit.
  Plain open-loop playback then runs the route, with no follower at all.

The measurement argues for the prefix. The follower's remaining value is for
worlds that are not empty, which nothing here tested.

## Limits

- **One route, one map, one axis pair.** The tolerance is specific to
  `1789686390-1-c0`; a route entered at speed, or one whose first takeoff is
  closer to its start, will differ.
- Along-track and cross-track are inferred from the first 475 ms of
  displacement, not decomposed per mark.
- Every run is on an empty world with no other players. Playback and the
  follower both react to nothing, so none of this measures contention.
- The cliff between 28 and 32 units along X was not bisected further, and the
  non-monotonic steering results were not repeated. Given that route times are
  quantised and the floor is stochastic, single runs here are indicative only.

## Two defects found and fixed on the way

- Skipping the placement teleport for the follower reintroduced a defect the
  route-asset work had already fixed: `bot_corner_start` clears the bot state
  and releases the hold issued before it, and the bot's own AI then walks it
  off the mark between the two rcon round trips. Measured at 47.9 units then,
  and at 40.8 units with a speed of 206 here - a start no tape assumes. The
  follower now places inside arming too, at `bot_routeFollowStart`.
- A mark that ended a run left the submit loop running, so commands kept being
  submitted after `Finish`. Guarded.

## Verification

Playback unchanged with the cvars unset: the approval run for
`1789686390-1-c0` reproduces 14,825 ms and 37.832 max drift, the record's own
figures. 94 movement tool tests pass.
