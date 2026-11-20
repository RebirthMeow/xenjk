# The bot subject in the route recorder, and the rescue switch removed

Recorded 2026-09-21. The first step of the
[transfer question](../../archive/movement-2026-10-05/PLAN.md#the-transfer-question-2026-09-21-does-the-fast-chain-survive-a-different-start-class),
which expected no source change and said that if one were needed to record a
bot, that was the first finding and the work stopped there until it was made.
One was needed. It was found, reported, directed by the owner and made, and
this record is what the new build measures.

> **Result.** On a fresh build carrying both edits, `red_out_L1_stand` pinned
> at start class 125 completes in **15,925 ms with 13 straight commits and 5
> state commits, both with the recorder off and with it armed** - the rescue
> screen's own class-125 control cell, to the millisecond and to the decision.
> The build is the control from here on, and the recorder is inert on the trip.
> The recording is retained beside its world.

Build `.codex-research/bot-recorder-build-20260921-a`; worlds under
`.codex-research/bot-recorder-verify-20260921-a/`.

## The finding: the recorder refused bots by construction

`Arm()` in `bot2_route_record.c` rejected `(ent->r.svFlags & SVF_BOT)`
alongside spectator, noclip and dead, and there was no other way in: `Arm` was
reached only from `RouteRecord_Command`, which was reached only from the
client command and the `route_record` console command. Everything downstream
was already client-agnostic and already ran on a bot's frames -
`RouteRecord_Before` and `_AfterPmove` sit in the shared Pmove wrapper on the
non-bench path in `bot2_movebench.c`, `RouteRecord_AfterEffects` in
`ClientThink_real` after `ClientImpacts` - and the clock, the start condition
and the `enemy_flag_pickup` terminator made no reference to humanness. The
refusal was the whole of it.

Two further gates belonged in the same decision and were reported with it. The
metadata wrote `"human": true` unconditionally, and `human_route_report.py`
refused any recording whose metadata did not carry it, so an unmodified writer
would have labelled a bot recording a human demonstration - the one thing the
record set must not become. `route_approve.py` had no start pin and recorded
whatever corner level it landed on, which steps 2 and 3 of the order need; that
is a tool change, not a source change, and it was made for step 2 below.

## What was built, under the owner's direction

The console command still refuses a bot, message unchanged. One internal entry
was added that only the corner-start command calls, on the release frame after
it has placed the bot and cleared its state, gated on `bot_routeRecord 2` - a
new value of the existing cvar, so no new switch joins the list principle 7
exists to prevent. At 0 or 1 nothing happens and the world is today's.

`RouteRecord_ArmBot(client, releaseLevel)` checks the cvar, cheats, and that
the subject is a living released bot on a team in normal play, then arms the
recording through the same `ArmSubject` the human path uses. For a bot subject
the recording starts at arm with the release level as its start time, so row 0
is the release frame's own command and there is no wait for a non-zero one:
the release frame belongs to the trip whether or not it moves the body, and
the talk-button catcher is a human's. The file format tag is unchanged.
Metadata carries `"human": false`, `"subject": "bot"`, the observed
`corner_level`, and the binding settings - rewrite, edges, planner, state,
straight, successor aim, reroute, exit-speed weight and state preference -
read and never set. The clock string states the release root.

`human_route_report.py` now labels the two subjects as different kinds and
refuses to conflate them: a recording that declares no subject is a human
demonstration, because every file written before the bot subject existed is
one; a bot recording that also claims `human` does not qualify, nor does a
human one that does not; and a bot recording must declare the release level it
started from. The player and the converter's format were not touched.

The rejected library-rescue experiment was removed with this change, as the
third-revision order required of a rejected arm and as the owner directed:
`bot_moveLibraryRescue` and its reader, `Graph_AdmittedStrafe` and its
declaration, `MOVE_RESCUE_MAX` and its comment, the rescue pass in
`StraightSearch`, `RescueCensus`, the rescue telemetry line and the `rescue=`
field in the committed sequence. Its evidence is
[the screen record](2026-09-21-library-rescue.md), which is why the code is
gone.

## Verification, declared before it ran

Both edits rode in one rebuild. Every cell on this fixture is deterministic to
one frame, so reproducing the class-125 control cell is the inertness check,
and it is the same check whether one edit rode in or two. The tape that comes
out of step 1 is to be the explorer's first plan, so it should be recorded on
the build that is the control from here on, which is the build with the rescue
code gone.

**The fallback was stated before collection and was not needed:** had the
control world failed to reproduce within a frame, the combined build would not
have been debugged; a rebuild carrying the recorder change alone would have
been run against the same world, attributing the disturbance to one edit.

Because inert code has shifted candidate counts before without changing
decisions or completion times, the comparison is the completion time *and* the
commit count, not the time alone.

### Offline

The converter's timing regressions plus one new case for the bot's root:
`tools/test_route_asset_build.py` `BotSubjectTest` asserts that a release row
converts with its real recorded duration (8, 25 and 32 ms), that a zero-input
release row is still tape index 0 rather than a row the converter skips, that
the next command chains from it, and that the built asset's start is the
release state with the release command counted in the duration. Six cases in
`tools/test_human_route_report.py` `SubjectTests` cover the subject labelling
and the three ways a mislabelled recording fails to qualify. The whole Python
suite is **684 tests, all passing**, from the repository root.

### Native

Case `red_out_L1_stand`, `--corner-class 125`, one world per arm, sequential,
settings as the rescue screen: `bot_moveSuccessorAim=1 bot_moveReroute=1
bot_moveExitSpeed=500 bot_moveStatePrefer=1 bot_moveSuccessorFan=0
bot_forecastItems=0 bot_forecastTriggerShadow=0`, playback and heading sweep
off, the 2026-09-16 seeded graph, `--edges --parity 256`.

| World | `bot_routeRecord` | Result | Corner level | Straight commits | State commits | Parity compared / mismatches |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| `cvar0-r2` | 0 | pass **15,925 ms** | 3125 | 13 | 5 | 667 / 0 |
| `cvar2-r1` | 2 | pass **15,925 ms** | 3125 | 13 | 5 | 667 / 0 |

Zero deaths, movement owner throughout, zero refusals in both. The rescue
screen's class-125 control worlds are 15,925 ms with 13 straight and 5 state
commits at corner levels 3325 to 4525, so the new build reproduces the cell
and the decisions, not just the number. Parity comparison counts scale with
the world's pre-release frames and 3125 is the earliest release of the set,
which is why 667 sits below that campaign's 677 to 727.

`cvar0-r1` never landed on the pinned class inside its window and was given up
after four attempts ("overshot at 5600"). It is retained. A pin miss is the
fixture's wall-clock lottery, not a result.

## The recording

`cvar2-r1/red_out_L1_stand/base/bot2-route/1790005094-0-c0.{csv,meta.json,summary.json}`,
beside its world.

636 rows, `complete: true`, `reason: enemy_flag_pickup`, `subject: "bot"`,
`human: false`, `corner_level: 3125`, `armed_stationary: true`,
`teleport_seen: false`, `start_carrying: false`, `active_powers_seen: 0`,
`force_decrease_seen: false`, `pmove_fixed: 1`, `pmove_msec: 8`.

Row 0 is the release frame's own command at level 3150 - `cmd_time` 3152
against `pre_cmd_time` 3128, a real 24 ms duration and three fixed steps -
from a stationary grounded root with zero velocity. The terminal row is level
19025 carrying the enemy flag.

**The two clocks differ by one server frame and should.** The recorder's own
clock runs release (3125) to the Pmove call where possession appeared (19025)
and reads **15,900 ms**; the harness reads **15,925 ms**, observing the pickup
on the following frame. This is the same one-frame offset the retained
[route assets](2026-09-18-route-assets.md) show, where every approved replay
came back +25 ms. Neither number is wrong and the record quotes both.

## Step 2, the self-control: it does not reproduce

Asset `.codex-research/bot-route-assets-20260921/1790005094-0-c0.route.json`
(636 commands over 15,904 ms of physics, 19 takeoffs, 69 contract marks, start
matching the fixture case exactly: origin 152/-1160/1924.125, yaw 90, velocity
zero, force 0). Worlds under
`.codex-research/bot-route-selfcontrol-20260921-a/`, played through
`tools/route_approve.py` on the same build, three worlds pinned to start class
125 at corner levels 5325, 4925 and 4725. One world was given up on the pin and
is retained.

> **The self-control fails, and fails identically in all three worlds:**
> `rejected` / `tape_end`, **15,925 ms**, **max drift 31.839**, 69 of 69 marks,
> no flag. The replay runs the whole tape in the control cell's own time and
> misses the objective.

### Where it breaks: one landing, command 583

| Command | Kind | Drift | Replay speed | Recorded speed |
| ---: | --- | ---: | ---: | ---: |
| 560 | checkpoint | 0.325 | 436.422 | 436.439 |
| 580 | checkpoint | 0.343 | 489.324 | 489.341 |
| **583** | **landing** | 1.159 | **405.335** | **426.024** |
| 584 | takeoff | 1.942 | 405.335 | 426.024 |
| 600 | checkpoint | 15.255 | 450.492 | 474.927 |
| 606 | landing | 20.300 | 467.753 | 491.956 |
| 620 | checkpoint | 31.839 | 499.998 | 527.241 |

For 580 of 636 commands the replay tracks the recording to **0.343 units and
0.017 units/sec** - the repaired player's own documented fidelity, inside the
0.29 to 0.35 the plan required of it. Then one landing. The replay arrives at
command 583 **0.283 units lower** (z 1859.802 against 1860.085) and leaves it
with **20.689 units/sec less**. One grounded 8 ms step at 426 units/sec costs
about 20.4, so the loss is what one additional grounded physics step would cost;
that is consistent with a sub-unit arrival difference deciding a ground contact
by one step, and it is not proof of the mechanism. Everything after 583 follows
from it.

So the failure is not the player wandering off the line. It is a trip with a
landing whose outcome is decided below the player's floor.

### Two things this settles on the way

**The replay is deterministic across absolute level.** Three different corner
levels of class 125 - 5325, 4925, 4725, spanning 600 ms - returned the same
15,925 ms and the same 31.839 drift to the millimetre. Within the class, the
absolute level changed nothing about the replay. This is not an answer to the
transfer question, which asks about *other* classes and needs a replay that
completes.

**It is not tape angle rounding.** The converter stores view angles rounded to
four decimals, about 4e-6 degrees per command, worth roughly 5e-4 units over
the whole trip - three orders of magnitude short of 0.34. The bot's command
angles carry a *constant* `delta_angles` offset of 234.997559 degrees (checked
at rows 0, 562 and 583), so unlike a human's, a bot's raw 16-bit command angles
are exactly recoverable. Reissuing those rather than re-deriving them from
degrees is the obvious candidate for closing the drift, and it is **untested**:
the order forbade touching the player or the converter's format, and nothing
here did.

### What was attempted and is not available

Replaying the tape at its own recorded level, 3125, would separate "the player
cannot reproduce this trip at any class-125 level" from "only at its own". It
cannot be run through `route_approve.py`: the tool's startup - server up, rcon
status, `addrunner`, hold - consumes more level time than that, and the first
play-log row it can read is already at 4425, 4625 and 4675 in three attempts.
The class pin works because it waits for the *next* class member. Reaching 3125
would need the startup sequence changed, which is not this order's business.

`tools/route_approve.py` gained `--corner-class` and `--corner-level`,
implemented as `movement_bench.py`'s own wait on the same play log with the
landed level verified afterwards; a world that never stands on the pin is
reported `inconclusive` / `corner_class_miss` and retained, never counted. That
is a tool change, not a source change.

## Where the order stands

The plan's third reading applies: **the self-control does not reproduce, so
neither reading of the class difference is drawn and the transfer step does not
run.** Its remedy is a repair with a regression shown to fail first, then a
rerun - and the repair target is the 0.34 units itself, because this trip has a
landing that cannot tolerate them. The `delta_angles` lead above is where to
start looking; it is a hypothesis, not a diagnosis.

## What this does not establish

The build and the instrument are verified; the transfer question is not
answered and no explorer is justified by anything here. The replay's 15,925 ms
is the tape's own duration, not evidence that the chain is executable - the run
it names did not reach the flag. Tape times, when a passing self-control
produces them, are prepared-plan results and move no gate row. One case, one
leg, one map.
