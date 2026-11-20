# Base defender, 2026-09-28

Owner, 2026-09-28: "position around the flag, shoot the bad guy, if he takes
your flag chase him, if he is getting too far pull him, ideally shooting and
pulling". The owner also asked to study it from demos, including 2v2 games.
The demo study is [its own record](2026-09-28-base-defender-study.md).

This builds on the [strafing chaser](2026-09-28-strafing-chaser.md): the
defender moves with the runner's own searches as a guest, and has Speed.

Build `defender-build-20260928-i`. Worlds and the analysis are in
`.codex-research/basedef-20260928-b/` (`campaign.sh`, `analyse.py`).
Build `-h`'s campaign (`basedef-20260928-a`) is kept; it counted a pressed
fire button as a shot (below).

## What the defender does (`bot2_defender.c`)

Each part is off by default and declared per world.

- **Guard (`bot_defenderGuard`, bench `--defender-guard`).** Between chases
  it holds one of its posts.
  - The posts are `guard_posts` in the fixture's `posts.json`: the study's
    four largest 4v4 base clusters per side on Kejim, ordered by share of
    home time.
  - It holds the first post. While the runner is within 1,500 u of the
    defender's own stand, it moves to the post nearest the runner's line to
    the stand; another post must be at least 96 u better to switch.
  - It moves by the movement owner, as a guest when strafing.
- **Weapon (`bot_defenderWeapon`).** The release gives it the weapon with
  full ammo. It shoots the perceived runner within
  `bot_defenderShootRange` (default 1,500), along a clear line from the eye.
  - Aim leads a straight projectile by its speed.
  - A splash weapon aims at the floor under the predicted position when the
    target will be near it, and directly at the target otherwise.
  - Aim is exact at the perceived state: a declared simplification, with the
    reaction delay (`bot_defenderReactMs`) as the only error.
  - A shot is logged (`FIRE`) only when the weapon spent ammo. Each damaging
    hit, from the defender's own `PERS_HITS`, is a `DAMAGE` line.
- **Carrier pull (`bot_defenderPullCarrier` D, `bot_defenderPullMax`).** It
  pulls the runner carrying its flag when it is between D and the maximum
  away (the pull's reach is 1,024).
  - `bot_defenderPullRate` optionally also requires the runner to be
    receding. It is unset here: the study found humans pull carriers at a
    median range rate of about 0.
  - `bot_defenderComboMs` pulls only within that long after a shot, i.e.
    fire, then pull as soon as the refire time allows.
- **Priority each frame:** a lethal pit throw (as before), then the carrier
  pull, then a shot.

## Arms

Kejim, classes 50 and 150, the same case sets as the chaser campaigns:
returns (`--filter _ret_`, 36 worlds an arm) and pickups (`--skip-twins`,
48 worlds an arm).

- **Settings in every defended arm:**
  - `--defender 1` (lethal throws);
  - reaction 250 ms;
  - chase, strafe and Speed;
  - guard posts;
  - rocket launcher (weapon 11);
  - carrier melee at range 96.
- **The arms:**
  - **rocket:** no carrier pull;
  - **base:** carrier pull at 350-700 u, the study's interquartile pull
    distances;
  - **combo:** base, plus pulls only within 1,200 ms of a shot. The study's
    humans pull about 1,020-1,090 ms after firing.
- **Controls:** the chaser campaign's no-defender controls on build `c`,
  reused because the return control on build `i` is identical in all 18
  worlds at class 50.

## Results

| Arm | Worlds | Carrier killed | Legs failed | Carrier deaths by | Pickup to carrier death, median | Hits per shot |
|---|---|---|---|---|---|---|
| control, returns | 36 | 0 | 0 | - | - | - |
| rocket, returns | 36 | 10 | 0 | rocket splash 8, direct 2 | 1.2 s | 27% (72/271) |
| base, returns | 36 | 9 | 0 | splash 7, direct 2 | 1.2 s | 26% (77/295) |
| combo, returns | 36 | 9 | 0 | splash 7, direct 2 | 1.2 s | 27% (78/293) |
| control, pickups | 48 | 0 | 0 | - | - | - |
| rocket, pickups | 48 | 14 | 1 | splash 11, direct 3 | 2.3 s | 26% (173/657) |
| base, pickups | 48 | 19 (18 worlds) | 2 | splash 18, direct 1 | 2.8 s | 30% (181/611) |
| combo, pickups | 48 | 17 | 1 | splash 16, direct 1 | 2.8 s | 30% (188/627) |

- **The defender now defends.** The chaser without a gun killed no carrier
  on a return and two per pickup arm (the stand exit). With the rocket it
  kills the carrier in about a quarter of the returns and 30-40% of the
  pickups.
- **Returns.** The kills come 1.2 s into the carry, before the carrier is
  out of the defender's base: a return case starts carrying on the enemy
  stand, which is where the guard stands. Once the carrier is away, the
  chase does not add kills.
- **Pickups.**
  - The carrier pull adds 3-5 kills over rocket alone (combo 17, base 19,
    against 14): 11 of 57 carrier pulls in the base arm end in the
    runner's death within 4 s.
  - Pulls alone do not kill: they bring the carrier back into rocket range.
  - The combo is no better than pulling on its own. With a 900 ms rocket
    refire, most pulls fall within 1,200 ms of a shot anyway.
  - This matches the study: the combo did not change outcomes in 4v4 and
    added some kills in 2v2.
- **Failed legs are defences.** `blue_out_L1_respawn4` at class 50 (every
  arm) is the attacker killed before it reached the flag.
  `blue_ret_L1_stand` at class 150 (base) is a Speedless level-1 carrier
  killed before it could capture.
- **Hit rate.** 26-30% per shot fired, at median ranges of 456-585 u. The
  study's humans are 47% with the rocket (an aimed enemy showing pain) at a
  median of 290 u. The bot shoots from farther: its range is 1,500 u, and
  its posts put it 500-800 u out.
- **Other deaths.** Runner deaths not carrying (the linger's next outbound
  leg, or before the pickup) add 0-8 an arm. The defender died 0-1 times
  an arm, to the carrier's melee.

**Shot counting fixed (build `h` to `i`).** On build `h`, a fire press
under a throw's hand extend was logged as a shot on every frame it was
held. That inflated the base arms' shots (543 against 294) and deflated
their hit rate (15%). Build `i` does not press fire under a hand extend,
and logs a shot only when ammo is spent. Carrier-kill counts moved by at
most 5 an arm between the two builds (pickups, base: 14 to 19): a press
that did not fire could still delay the pull behind it.

## Reading

- A study-tuned base defender is a real threat to the current runner:
  carrier kills went from 0-2 an arm to 9-19. The runner has no absorb, no
  gun, no dodge, and plans its routes blind to the defender.
- The pull matters on pickups, where the carrier leaves from beside the
  defender. On returns the kills all come in the first second and a half.
- This is the measured opponent for milestone A (take a defended flag and
  escape usefully): a baseline the runner's situation model and Force
  choices (absorb at pit edges, protect when hurt, Speed) can be scored
  against.
- **Open, the owner's to choose:**
  - aim error to match human hit rates;
  - shorter engagement ranges, as humans fire at a median 290 u;
  - other weapons: the repeater alt is lobbed and not modelled;
  - pit-directed pulls;
  - the runner's first responses.
