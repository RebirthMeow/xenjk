# Defender gaps against the owner's 1v1 (2026-09-29)

The owner compared the defenders' rules with the
[human defense session](2026-09-29-human-defense-session.md): "leash should
be a non-factor, its 2v1. rest seem legit, address them". This record covers
the changes and one laps campaign. It follows the
[review fixes](2026-09-29-defender-review-fixes.md).

## What the owner's session showed against the rules

- **Engage latency.** From the runner entering the owner's view (in the
  snapshot, within 1,500 u and 50° of the view yaw, after at least 1 s out of
  it) to the first shot: p25 / median / p75 575 / 1,075 / 1,362 ms (9
  episodes, `.codex-research/aimfix-20260929-a/owner_engage.py`). That is
  close to the bots' 1,200 ms engage delay.
- **What blocks the bots' shots.** A single frame out of sight, a weapon that
  is not ready, or a throw's hand extend all restarted the delay. Among frames
  with the runner in sight and in range, the delay blocked about 70% for chase
  D and 40% for base D (`PRESHOT`, four laps worlds).
- **Throws.** Over 369 lethal throws in eight laps worlds, 26% killed. Throws
  from 400-600 u killed 50%, against 10-22% from other ranges. With 0
  escapes, 62% killed (9 throws). Carrier pulls killed 13%. The owner threw
  from a median of 458 u; 12 of 15 of the owner's throws were followed by a
  death.

## Changes (`bot2_defender.c`)

- **Leash (default).** Base D's `bot_defenderLeash` now holds only when the
  attacking team has more than one player. Against a lone attacker it chases.
- **`bot_aimEngageGraceMs G`** (default 0). The engage delay restarts only
  after G ms out of sight or range. A weapon that is not ready and a throw's
  hand extend no longer restart it.
- **`bot_defenderFollowMs M`** (default 0). For M ms after a throw hits,
  shots skip the engage delay: pull, then shoot.
- **`bot_defenderThrowMin/Max`** (default 0 and 1,000). Lethal throws only
  from within this band. Outside it the defender shoots instead.

## Measured

Build `gaps-build-20260929-a`. The worlds, `run.sh` and `compare.py` are in
`.codex-research/gaps-20260929-a/`. Laps worlds use the review-fix
configuration: both defenders, every behaviour, the owner's loadout, walked
errands, 10 game minutes each. `pre-leash` is the four review-fix "after"
worlds (`aimfix-build-20260929-a`). Rates are per 10 game minutes. "Stand"
stops come within 3 s of the grab, "route" stops after that.

| Arm | Worlds (distinct) | Captures | Grabs | Carrier stops | Stops per grab | Stand | Route | Runner deaths | Fires | Lethal throws | Kill % | Defender falls |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| pre-leash | 4 (4) | 14.4 | 19.6 | 5.0 | 25% | 3.0 | 2.0 | 13.6 | 45.8 | 30.5 | 30% | 2 |
| ctl (leash off) | 4 (4) | 14.6 | 20.6 | 5.0 | 24% | 1.3 | 3.7 | 10.3 | 60.9 | 45.0 | 15% | 3 |
| grace 1,000 | 4 (4) | 14.7 | 20.8 | 5.4 | 26% | 3.1 | 2.3 | 10.5 | 66.8 | 37.8 | 24% | 4 |
| follow 3,000 | 4 (4) | 14.5 | 20.4 | 5.8 | 29% | 2.6 | 3.2 | 10.3 | 57.7 | 35.2 | 22% | 2 |
| window 400-600 | 4 (4) | 16.1 | 22.0 | 5.7 | 26% | 1.7 | 4.0 | 8.2 | 58.9 | 21.5 | 19% | 4 |
| all three | 4 (4) | 14.6 | 21.8 | 6.7 | 30% | 2.1 | 4.5 | 10.9 | 71.8 | 17.3 | 29% | 4 |

Captures per world: pre-leash 18, 15, 12, 13; ctl 14, 15, 15, 11; grace 15,
12, 15, 15; follow 15, 14, 15, 11; window 13, 18, 15, 19; all 15, 15, 11, 14.

## Reading

- **Captures did not move in any arm.** They stay at 14.4-16.1 per ten
  minutes, inside the 11-19 spread between worlds.
- **Leash.** With it off, base D follows the carrier: stops move from the
  stand onto the route (route stops 2.0 → 3.7). The total stays at 5.0, and
  lethal throws go up with a lower kill rate (30% → 15%). Base D now throws
  on the chase too.
- **Grace and follow-up.** Fires rise (61 → 67 and all three 72), and stops
  per grab creep up (24% → 26-30%). With four worlds an arm, these small
  shifts are not resolved.
- **Throw window.** It halved the lethal throws but did not raise their kill
  rate (19%, against 15% in ctl). The 50% at 400-600 u was a property of the
  situations where the defenders happened to throw from there, not of the
  range. The band is not a lever, and it stays off.
- **The owner's level is far off.** The owner stopped 4 of 5 carriers; the
  best arm stops 30% of grabs. Firing rate is still about half the owner's
  (72 against about 130 per ten minutes). The rule differences addressed here
  are not what separates the bots from the owner.

## Hits and damage per carry

Owner: "count the hits and damage per carry". The measures:
- **Damage** comes from the runner's own play log (250 ms samples) in every
  world, the owner's included, and is the sum of health drops over a carry.
  It includes the runner's own fall damage.
- **Hits** are the defenders' `DAMAGE` lines (their `PERS_HITS`, both bots),
  and for the owner, `PERS_HITS` rises in the POV demo. These count saber
  hits too.
- **Scripts:** `carry_damage.py` and `owner_carry_hits.py` in the campaign
  directory.

| | Carries | Carriers killed | Damage per carry p25 / median / p75 | Health at the grab, median | Hits per carry | Hits per second carrying | Hits per second overall |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Owner (1 player) | 4 | 3 | 10 / 28 / 77 | 54 | 1.75 | 0.22 | 0.18 |
| Bots, 6 arms (2 players) | 74-88 an arm | 19-25 | 0-6 / 12-17 / 38-48 | 68-90 | 0.47-0.67 | 0.038-0.059 | 0.046-0.065 |

- **Hits.** The owner alone landed about four times the hits per second of
  both bots together, during carries and outside them.
  - **Fewer shots.** The bots fire about half as often (72 against about 130
    per ten minutes, best arm).
  - **Fewer hits per shot.** About 49% of the bots' shots hit, against about
    80% of the owner's (39 hits from 47 counted shots, splash included).
- **Carries the bots hit, they rarely finish.** About three quarters of bot
  carries end in a capture. Two thirds of those surviving carriers took
  damage, and they kept a median minimum health of 48-79.
- **Softening before the grab.** Three of the owner's four carries started
  at 9-63 health and all three died. The bots' grabs start lower than full
  too, partly from the runner's own falls, and carriers below 70 health die
  36-53% of the time against 9-18% above.
- **Sample.** The owner's side is 4 carries and 7 hits, a direction rather
  than a rate.

## Misses by weapon, range and airborne; the golan aim

Owner: "break down the misses by weapon, range and airborne", then pointed at
the golan hit-envelope lesson (`codemp/cgame/cg_aimfeedback.c`,
[golan_fixed_frame_lesson.md](../../golan_fixed_frame_lesson.md)).

**Misses.** Each `FIRE` is paired with the hits it produced: a `DAMAGE`
line of the same defender whose `since_shot` points back at the press,
within the flight time plus 400 ms. 1,401 shots from 24 laps worlds (the
gaps and review-fix arms, `bot_aimStyle 2`) hit 33%. The script is
`.codex-research/gaps-20260929-a/shot_hits.py`.

| Weapon | Shots | Hit | 0-150 u | 150-300 | 300-500 | 500+ |
| --- | --- | --- | --- | --- | --- | --- |
| Repeater alt | 656 | 52% | 80% | 46% | 46% | 14% |
| Rocket | 180 | 46% | 68% | 59% | 33% | 8% |
| Flechette alt | 565 | 8% | 13% | 7% | 6% | 8% |

- **Airborne makes no difference.** Hit rates are the same airborne or
  grounded, for every weapon.
- **Flight time decides.** Shots arriving in under 150 ms hit 60%, and
  shots over 500 ms hit 9%.
- **Role.** Chase D hits 16% of its shots, base D 50%. Most of chase D's
  shots are flechette.

**The flechette alt was aimed at the floor.** Its balls bounce off the world
(`FL_BOUNCE_HALF` in `G_MissileImpact`). They burst only on a player, or on
their 1.5-3.5 s timer. The kernel treated them like a repeater alt burst and
aimed at the floor under the target. The kernel (`Model`) and the defender's
floor gate (`Splashes`) now aim them at the body. This change is on by
default, and its effect alone has not been measured.

**The lesson's solver in the kernel** (`bot_aimGolan 1`, default 0;
`GolanRefine`).
- **Method:** it samples the balls' random speed, lift and sideways spread
  with the lesson's Halton rolls. The rolls are scored against the target's
  predicted path, in place of the lesson's recorded one. It searches a 2°
  and then a 1° view grid for the best P(at least one of two hits directly).
- **Weapon choice** uses that chance. `bot_aimGolanMin` holds shots below a
  chance.
- **Measured:** laps worlds, gaps ctl configuration, campaign
  `.codex-research/golan-20260930-a/` (`compare.py`).

| Arm | Worlds | Flechette shots | Flechette hit | Shots rated 60-100%: hit | All shots hit | Captures /10 min |
| --- | --- | --- | --- | --- | --- | --- |
| ctl (floor aim, table choice) | 8 | 179 | 9% | - | 30% | 14.6 |
| solver | 8 | 524 | 4% | 8% of 249 | 18% | 15.8 |
| solver, hold below 0.2 | 4 | 229 | 6% | 7% of 170 | 16% | 16.0 |
| solver, aim error off | 4 | 276 | 7% | 9% of 156 | 24% | 14.5 |
| solver, forecast-scaled chance | 8 | 495 | 5% | 14% of 124 | 15% | 15.4 |

- **The aim error does not explain it.** With the error off, the solver's
  60-100% shots still hit 9%. The repeater alt, though, rose to 70%.
- **The forecast does.** The lesson scores the recorded path; live, the
  path must be forecast.
  - The runner's own log shows a straight-line forecast off by a median of
    13, 51, 101 and 160 u horizontally at 250, 500, 750 and 1,000 ms.
  - The runner's hull is 18 u from centre to edge once the ball's radius is
    added.
  - So a direct hit is only forecastable under about 250 ms of flight,
    about 250 u. The owner fired flechette alt at a median of 121 u.
- **Scaling the chance by that error** orders the buckets correctly (hits
  1, 2, 8, 13, 14% from lowest to highest), but the chances are still 2-4
  times the hits.
- **Weapon choice gets worse.** The solver's chance draws weapon choice to
  the flechette. Weapon choice has no minimum, so long lobs fire whenever
  nothing else scores. Hits per shot fall from 30% to 15-18%.
- **Captures do not move** in any arm.
- **Status:** the solver stays behind `bot_aimGolan`, off.

## What the defender aims from

The owner asked whether the defender knows the enemy's speed. It knows
position, velocity and grounded state, but as perceived
`bot_defenderReactMs` (250 ms) ago (`Perceived(react)`). The kernel leads
that state by the shot's flight time only.
- **`bot_aimLeadReact 1`** (default 0) first carries the perceived state
  forward by its age (`Bot2_AimAdvance`), using the kernel's forecast.
- **Repeater alt boost.** The kernel now adds the repeater alt's 40 u/s
  upward boost (`WP_RepeaterAltFire`), which the aimfeedback study already
  modelled. This change is on by default, and its effect alone has not been
  measured.
- **Measured:** campaign `.codex-research/lead-20260930-a/`, build
  `lead-build-20260930-a`, gaps ctl configuration, golan solver off, four
  worlds an arm (control: eight).

| Arm | Repeater alt hit (0-150 / 150-300 / 300-500 u) | Rocket hit | Flechette hit | Captures /10 min | Stops per grab | Runner deaths |
| --- | --- | --- | --- | --- | --- | --- |
| ctl | 47% (77 / 44 / 37) | 45% | 9% | 14.6 | 27% | 10.0 |
| error off | 60% (79 / 52 / 55) | 77% | 6% | 14.4 | 30% | 11.6 |
| error off, dead-reckoned | 67% (100 / 80 / 61) | 56% (18 shots) | 7% | 16.3 | 20% | 8.5 |
| error off, reaction 0 | 81% (100 / 82 / 79) | 87% (15 shots) | 16% | 11.9 | 38% | 17.5 |
| human error, dead-reckoned | 61% (95 / 64 / 60) | 62% (16 shots) | 5% | 14.6 | 27% | 9.7 |

- **The ceiling.** With a current view and no error, the splash weapons hit
  nearly everything inside 500 u. Flechette alt reaches 58% at 0-150 u and
  39% at 150-300 u.
- **Captures move only in the full-aimbot arm** (11.9 against 14.6, stops
  38%). Reaction 0 also gives the throws a fresh view: their kill rate rose
  to 37%.
- **Dead reckoning helps less than a fresh view.** Carrying the stale state
  forward adds its own forecast error.

## Human-like aim, now the default

Owner: "go human-like, 100ms aim view, flechette under 250u".
- **`bot_aimViewMs 100`** (default). The aim tracks the runner as seen
  100 ms ago; engaging and throwing still use `bot_defenderReactMs`.
- **`bot_defenderFlechetteMax 250`** (default). Beyond 250 u the defender
  switches off the flechette alt at once, and never fires one.
- **Also on:** the repeater alt boost and the flechette body aim, both from
  above.

Build `human-build-20260930-a`, four laps worlds, gaps ctl configuration
(`.codex-research/human-20260930-a/`, `compare.py`), against the eight ctl
worlds:

| | ctl | human-like |
| --- | --- | --- |
| Hits per shot | 30% | 48% |
| Repeater alt (0-150 / 150-300 / 300-500 u) | 47% (77 / 44 / 37) | 58% (100 / 67 / 42) |
| Rocket | 45% | 58% |
| Flechette alt | 9% (all ranges) | 19% (all within 250 u) |
| Shots per 10 min | 53 | 37 |
| Captures per 10 min | 14.6 | 14.8 |
| Stops per grab | 27% | 22% |

- **Aim is better, captures are not.** The defenders fire fewer, better
  shots, and captures are unchanged. Only the full-aimbot arm has moved them.
- **Video.** Chase D's carrier kills in `human-3` are cut from its
  first-person demo (`video/cut.sh`, `video/align.py`). They are rendered by
  the installed game's client as local demo playback.

## Positioning: base D blocks, chase D pursues (2026-09-30)

Owner, after watching the video: base D stood still near its mines instead
of getting in the way; chase D never really chased. The requests:
- stand on the runner's route when it is known to be coming;
- aim directs when close;
- chase D should strafe earlier, then pull to catch up and shoot.

**Built** (`bot2_defender.c`; the header lists the cvars).
- **`bot_defenderHearRange 1500`.** The runner within 1,500 u is known when
  unseen, for positioning only.
- **`bot_defenderBlock`.** While the flag is home and the runner is known
  within 2,500 u, base D stands on the runner's graph route to the stand:
  the first cell 200-700 u out that it reaches first. The block outranks
  rearming and mines.
- **`bot_aimDirectRange 200`** (default). Inside 200 u, splash weapons aim at
  the body.
- **`bot_chaserPursue`.** While the flag is home, chase D strafes after a
  runner known within 2,500 u of the stand.
- **`bot_chaserPullMax`.** Chase D also pulls a receding carrier out to
  that distance.

**Measured.** Build `pos-build-20260930-a`, four laps worlds an arm, gaps
ctl configuration (`.codex-research/pos-20260930-a/`). `chase` is pursue
plus `bot_chaserPullMax 1000` plus `bot_defenderFollowMs 3000`.

| Arm | Captures /10 min (per world) | Grabs | Stops per grab | Runner deaths | Hits per shot | Throw kill % | Defender falls |
| --- | --- | --- | --- | --- | --- | --- | --- |
| pctl (defaults) | 14.9 (15, 15, 16, 10) | 20.8 | 26% | 10.7 | 40% | 22% | 4 |
| block | 14.3 (16, 12, 17, 9) | 18.0 | 19% | 10.8 | 44% | 22% | 4 |
| chase | 14.2 (13, 15, 15, 13) | 19.6 | 25% | 10.9 | 41% | 23% | 8 |
| block + chase | 12.3 (13, 12, 13, 11) | 18.4 | 32% | 14.9 | 54% | 35% | 12 |

- **Both together.** This is the first human-like-aim arm to cut captures:
  every world is at 13 or below, against 10-17 before. Runner deaths rise
  40%, and hits per shot reach 54%.
- **Activity.** Base D took 387-445 block goals in four worlds. Chase D
  pursued 117-143 times, and 19-24 carrier pulls came from beyond 700 u.
- **Direct shots up close do not help** at human error. Rocket inside
  150 u hit 33-64% in these arms, against 78% with the floor aim
  (`human-20260930-a`, nine shots). The samples are small. A roughly 8°
  error is about 21 u at 150 u, wider than the body.
- **Defender falls rise** with the pursuit (4 to 8-12 in four worlds).

## Chase D returns the dropped flag (2026-09-30)

Owner: "if we kill flag carrier then flag drops to ground, need to return
it to stand".
- **`bot_chaserReturn`** (default 1). While the own flag lies dropped
  (`FL_DROPPED_ITEM`), chase D strafes to it and touches it, which returns
  it (`Team_TouchOurFlag`). This comes before every other errand. Each return
  is logged as a `RETURN` line.
- **Measured:** build `ret-build-20260930-a`, four laps worlds, the
  block + chase configuration, against the four `pos-20260930-a` "all"
  worlds. The script is `.codex-research/ret-20260930-a/flags.py`.

| After a carrier kill | No return (22 kills) | Return (21 kills) |
| --- | --- | --- |
| Chase D returned | 3 | 8 (median 0 s) |
| Base D returned | 2 | 2 |
| Runner re-took it | 10 (median 13 s) | 9 (median 11 s) |
| Home by itself | 7 | 2 |
| Captures /10 min | 12.3 | 14.0 (16, 11, 13, 11) |

- **Returns are fast where they happen.** Chase D started 12 returns and
  finished 11. Most were at the stand, in 25-2,500 ms.
- **Most re-takes are in the pit.** 7 of the 9 were at z = 32, the chasm
  floor.
  - A carrier that falls into the chasm drops the flag on the pit floor. It
    is not a no-drop volume, so the flag stays there.
  - The respawned runner goes down after the flag, picks it up, and dies to
    the pit again.
  - Chase D does not follow into the lethal pit.
- **The other 2 re-takes** were on the chasm ledge at z 1368, where chase D
  took 11-14 s to arrive.
- **Captures** are within the spread between worlds.
- **A runner fault.** Its objective follows a dropped enemy flag into a
  lethal pit. That belongs to the runner's route choice, not the defenders.

**Correction (2026-09-30).** The re-take counts above are wrong, and so is
the pit finding. `flags.py` took the runner's first carrying sample after the
kill's whole-second log line, which is the runner's own death sample (still
carrying, where it fell) up to a second before the kill. It now requires a
respawn first (a non-carrying sample, then a carrying one); the old script is
kept beside it as `flags.pre-fix.py.txt`. Re-run: no return 2 re-takes (was
10), return 2 (was 9), goal-20260930-a ctl 0 and stand 1. The five re-takes
are at z 1901, 1940 (the upper floor), 1394, 1368 and 1829 (the chasm ledge);
**none is in the pit**, and the runner fault is withdrawn. Details:
[defender falls and route](2026-09-30-defender-falls-and-route.md#3-the-pit-re-take-is-a-measurement-artifact-not-a-runner-fault).

## New defaults: block, chase, and exact aim up close (2026-09-30)

Owner: "make both defaults, remove human like requirement for close range,
those can be full aimbot".
- **Now default.** `bot_defenderBlock 1`, `bot_chaserPursue 1`,
  `bot_chaserPullMax 1000`, `bot_defenderFollowMs 3000` and
  `bot_chaserReturn 1`.
- **Exact aim inside `bot_aimDirectRange` (200 u).** The aim uses the
  current runner state (`Perceived(0)`) at its body, with no view lag, turn
  cap, error, engage delay, pacing or splash gate.
- **A bug caught on the way.** `CvarFloat` was used in `Follow` before its
  declaration. C's implicit int return would have misread the value; the
  compiler warned (C4142). The forward declaration now sits at the top of
  the file, and no measured build had the bug.
- **Measured:** build `def-build-20260930-a`, four laps worlds, gaps ctl
  configuration with no block or chase switches passed
  (`.codex-research/def-20260930-a/`).

| Arm | Captures /10 min (per world) | Stops per grab | Stand / route stops | Runner deaths | Hits per shot | Chase D returns |
| --- | --- | --- | --- | --- | --- | --- |
| block + chase (`pos` all) | 12.3 (13, 12, 13, 11) | 32% | 3.5 / 2.3 | 14.9 | 54% | 3 of 22 |
| + return (`ret`) | 14.0 (16, 11, 13, 11) | 30% | 3.0 / 3.0 | 13.4 | 51% | 8 of 21 |
| new defaults | 8.1 (7, 11, 8, 5) | 54% | 8.9 / 0.5 | 26.8 | 61% | 28 of 36 |

- **Close-range hits.** Repeater alt hits 87% inside 150 u and 77% at
  150-300 u; flechette alt 69% inside 150 u; rocket 60% inside 300 u.
- **Where the kills come from.** Almost every stop is at the stand, within
  3 s of the grab. The block puts base D in the way, and the exact aim
  finishes the runner there.
- **Returns.** Chase D returned 28 of the 36 dropped flags, base D 5. The
  runner re-took 3.
- **Against the owner's 1v1.** The owner stopped 4 of 5 carriers (80%).
  The defenders now stop 54% of grabs.
- **Still open.** Defender falls are 16 in four worlds, from the pursuit
  across the chasm walkway, and route stops are rare.

## Chase D's falls, and the stand goal (2026-09-30)

**Why chase D fell** (default worlds, `def-20260930-a`). The owner asked
whether aiming overtook the strafe; it did not.
- **The pattern.** Before each of its 10 falls (9 distinct), chase D was
  chasing or pursuing a goal on the runner. The runner was airborne over the
  chasm or on a ledge off the walking graph. The movement reported
  `recover reason=no_path` and braked (`DeclineHeld`) for 1-3 s on the walkway
  beside the chasm (x 270-400, y -380 to -55, z 1808), then dropped to the pit
  floor at 0-12 u/s.
- **Not the aim.** The aim's view change turns the movement keys with it
  (`SetView`), the brake included. One fall followed chase D's own
  repeater alt shot by 50 ms (a knock); one throw came while it was already
  falling.

**The owner's test** ("sets the enemy flag stand as the main goal and the
enemy FC as a sort of secondary goal").
- **`bot_chaserGoal 1`.** Chase D's movement goal is where the runner is
  going: the own stand before the grab, and the runner's stand with the
  flag. The runner is only shot and pulled. The stands are always on the
  graph.
- **Measured:** build `goal-build-20260930-a`, eight laps worlds an arm, new
  defaults otherwise. The control is `def-20260930-a` plus four more
  (`.codex-research/goal-20260930-a/`, `compare.py`, `flags.py`).

| | Control (8) | Stand goal (8) |
| --- | --- | --- |
| Captures /10 min | 8.2 (7, 11, 8, 5, 9, 9, 7, 7) | 1.9 (4, 2, 4, 1, 0, 1, 2, 1) |
| Grabs /10 min | 16.6 | 10.8 |
| Stops per grab | 51% | 83% |
| Runner deaths /10 min | 27.1 | 39.5 |
| Chase D hits per shot | 51% | 76% |
| All hits per shot | 62% | 76% |
| Chase D falls / base D falls | 15 / 11 | 12 / 10 |
| Dropped flags returned (chase / base) | 49 / 10 of 65 | 54 / 13 of 72 |

- **Against the owner.** The defenders now stop 83% of grabs; the owner
  stopped 4 of 5 (80%).
- **Stops are still mostly at the stand** (7.6 of 8.9). Both defenders are
  there when the runner arrives, and chase D, running the same way as the
  carrier, finishes it when it does break out.
- **Falls barely change.** 12 against 15 for chase D, so the no-path tail
  was not their only cause; this is still open.

**Correction (2026-09-30).** The stand goal's 1.9 captures and 83% stops do
not reproduce. `goal-build-20260930-a` re-run with the same settings gives
4.0 and 70% over eight new worlds, and HEAD gives 4.5 and 67%
(`.codex-research/falls2-20260930-a/`, `gctl` and `octl`). The eight worlds
above were a favourable set. Details:
[defender falls and route](2026-09-30-defender-falls-and-route.md#results-2026-09-30-implementing-session).

## Limits

- Four laps worlds an arm, one map. Laps worlds can replay (see the review
  fixes record); all 24 here are distinct.
- The engage-latency sight test uses the demo's snapshot (PVS) and view yaw,
  not a line-of-sight trace.
- The fixes are uncommitted.
