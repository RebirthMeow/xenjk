# Route assets: the bot executes the human route

The owner directed a different approach to the stand deficit: adapt a human
recording into an approved bot route offline, and have the bot execute it at
runtime, rather than extending the online search until it finds the same line.

It works. **The bot completed the fastest human demonstration in 12,900 ms
against the human's 12,875 ms**, reaching the enemy flag, on the recorded
line, reproducibly. All three demonstrations are approved.

Read the limits before the numbers: these runs start where the recordings
start, which is not the benchmark's stand fixture. See "What this is not".

## The pipeline

Three pieces, in order:

**`tools/route_asset_build.py`** turns a recording into an asset. The recording
is already a complete input tape - one row per authoritative Pmove call, with
`pmove_fixed=1` and `pmove_msec=8` - so the physics is a function of the
command sequence and the state it starts from. The asset carries the binding
(map, checksum, recorded physics, SHA-256 of the source), the exact start, the
tape, and a state contract of every takeoff, every landing and a checkpoint
every 500 ms. View angles are stored in degrees, not the recorded 16-bit
command angles, because those are relative to the recording client's
`delta_angles` and mean something else for another player.

**`codemp/game/bot2_route_play.c`** is the playback owner, hooked into
`G_RunClient` ahead of the flag runner and the bench, inert until armed. A bot
thinks every 25 ms and the tape is one command per 8 ms, so playback does not
emit one command per think: it drains the tape, dating each command from
`ps->commandTime` plus its own recorded delta, and stops when the next one
falls past the server frame. Three commands per frame, remainder carried. This
is the path a real 125 fps client already takes through the server.

It does not steer or correct. Divergence from the recorded line is the
measurement, reported at every mark and never hidden.

**`tools/route_approve.py`** stages a fixture world the way the movement bench
does, places the bot at the tape's start, arms playback, and writes a verdict.
Approval is not "it finished": it requires the enemy flag, no death, and drift
within 64 units at every recorded mark. A run that finishes off the line
finished some other route and its time means nothing.

## Measured

Build `move-build-routeplay-20260918-d`, worlds under
`.codex-research/route-approve-20260918-a`, assets under
`.codex-research/route-assets-20260918`.

| route | recorded | executed | delta | max drift | median drift | marks |
|---|---:|---:|---:|---:|---:|---:|
| `1789686350-0-c0` | 12,875 | **12,900** | +25 | 36.98 | 20.1 | 51 |
| `1789686390-1-c0` | 14,800 | **14,825** | +25 | 37.83 | 16.0 | 61 |
| `1789686416-2-c0` | 18,550 | **18,575** | +25 | 23.79 | 10.0 | 73 |

Every route is +25 ms, one server frame, which is the arming frame. The
fastest was run twice and returned 12,900 ms and a max drift of 36.984 both
times: the replay is deterministic.

The speeds track too. At 11.5 s into the fast route the bot is at 647.2
units/sec against the recorded 649.4. The search-driven bot never exceeds
about 520 at a landing in a stand run.

For scale, the bot's own search on the stand fixture is 19,150 ms median on
red and 19,925 on blue, over 18 repetitions.

## What this is not

- **These are not stand-fixture times.** Each route starts where its recording
  started. The benchmark stand is `(152,-1160,1924.125)`; the three recordings
  start 392.8, 35.7 and 9.0 units from it. The 12,900 ms run begins nearly 400
  units up the route and is not comparable to a stand time. The honest
  comparison is `1789686390-1-c0`: it starts 35.7 units from the stand and
  executes in 14,825 ms, against the search's 19,150.
- **It is open loop.** Nothing reacts. Another player, a dropped flag, a
  different spawn, any world state the recording did not have, and the tape
  plays on into geometry that has moved. Drift already reaches 37 units on a
  clean, empty, identical world.
- **It is one line on one map**, bound to a map checksum and a physics setting,
  and it refuses to arm against anything else.
- **It is not the search getting better.** The search's own deficit, measured
  in [where the stand time goes](2026-09-18-where-the-stand-time-goes.md), is
  untouched by any of this.

## Two defects the harness found on its own first runs

- The tape's map checksum was parsed through a float. A checksum is a full
  32-bit value and a float carries 24 bits of mantissa, so `-1301924053`
  arrived as `-1301924096` and the tape was refused against the map it was
  recorded on. Integers are now parsed as integers.
- Placement and arming were separate rcon round trips, and the bot's own AI
  walked it 47.9 units off the mark in the gap. `bot_corner_start` clears the
  bot state, which released a hold issued before it, so placement now happens
  inside arming, with the AI held and no window at all.

Both were caught by refusals that named what disagreed, which is what those
refusals are for.

## Verification

The 36-case default floor is 36/36 with the playback module present and
unarmed, so hooking `G_RunClient` costs nothing. Nothing arms itself. The
asset builder has 10 unit tests; 94 movement tool tests pass.

## Next

1. The follower: steer to the state contract instead of replaying blind, so a
   route survives a start that is not its own and a world that is not empty.
   That is what makes a route asset usable from the stand fixture.
2. A prefix from the stand to a route's first takeoff, which the existing
   search can do at walking speed, so a route can be entered rather than
   started.
3. Route selection at runtime: which approved route applies, and what the bot
   does when none does.
