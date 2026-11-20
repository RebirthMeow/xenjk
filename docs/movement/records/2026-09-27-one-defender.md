# One defender: push and pull against the runner, 2026-09-27

Roadmap step 3 started small (owner, 2026-09-27): instead of the full
milestone A plan, one defender that pushes and pulls appropriately against
the offense runner. "Faster routes tend to pass over pits, so let's see what
happens." The runner is unchanged and knows nothing about the defender.

## The fixture

`Defender_v2` (`codemp/game/bot2_defender.c`, fixture files in
`tools/fixtures/movement/defender/`), on the runner's enemy team.

- **Post.** Measured, not chosen: the median position of a Kejim base POV
  within 512 u of its own stand, with its flag home and an enemy within
  1,024 u (10 base demos of the position pilot, red folded onto blue by the
  map's point symmetry, 2,559 samples): (2440, 5204), 110 u in front of the
  blue flag toward mid. The corner start places it there and fills its Force
  (100). Saber holstered: it throws and does nothing else.
- **Loadout.** Push 3, pull 3 (20 Force each). The runner has neither, so it
  cannot counter a throw.
- **The throw is native** (`GENCMD_FORCE_THROW`/`PULL`, `ForceThrow`). A
  level-3 throw *sets* the target's velocity along the line between origins
  to 768 − 0.7 × distance u/s (at least 16), with at least 128 up when the
  target was not moving vertically. It reaches 1,024 u within about 53° of
  the aim, with line of sight.
- **When it throws.** Each frame it perceives the runner as it was
  `bot_defenderReactMs` ago (visible only with PVS and line of sight), aims at
  it, replays the native velocity rule for a pull and a push, and sweeps the
  runner's hull with no input under gravity until it lands, touches a lethal
  `trigger_hurt` or reaches 3 s. It throws when a throw is lethal and the
  runner's own flight is not; with both lethal, the earlier death. One throw
  is followed at a time.
- **Scored against the world.** Every throw logs the native hit (the
  target's push effect stamped at the throw's level time) or a miss, the
  runner's first landing against the predicted end with its airborne input
  frames, and the outcome within 4 s: pit (`fallingToDeath`), death, or
  survived.

Kejim has one lethal volume, a fall-to-blackness `trigger_hurt` (dmg −1)
under the whole map below z 704; the streets are at 1,368-1,924.

What the pilot says a human base does (`extract-v3`, 10 Kejim base demos):
345 pulls to 60 pushes; throws from the stand out to mid (distance from its
own stand: median 1,520 u, quartiles 785 and 2,963). Pits are 37% of
enemy-caused deaths, and 66% of a POV's pit kills follow its own push or
pull ([pilot record](2026-09-27-position-pilot.md)). This defender only
throws from its post, so it covers the stand end of that range.

## Arms

Kejim, seeded (`mapprep-20260927-a`), mesh not loaded, `--edges --parity 256
--linger 4 --skip-twins`, classes 50 and 150, 24 cases each.

- **Control:** no defender, build `padmatch-build-20260927-a` (the 4d9745c6
  tree), collected before any edit.
- **Identity:** the control at class 50 on the defender build
  (`defender-build-20260927-b`); every case must match, since no code the
  defender adds runs without one.
- **D0:** the defender at its post, never throwing (its body only).
- **D1 r0:** throwing, perceiving the runner in the current frame.
- **D1 r250:** throwing, perceiving the runner as it was 250 ms ago.

Outputs: `.codex-research/defender-20260927-a/`.

## Expectations before looking (Claude, 2026-09-27)

Written after the one smoke world (below) and before any campaign result.

1. Identity: 24 of 24 identical.
2. D0: little change. The post is off the flag, but outbound runs finish on
   the stand beside it; a few times move by a frame or two, no deaths.
3. D1 r0: throws come on the last approach to the stand, and returns get
   thrown as they leave. Fewer than half of the hits become pit deaths: the
   runner replans from wherever the throw leaves it and steers in the air,
   and the sweep assumes no input. Level 2 and 3 runs are thrown more often
   than level 1 (faster, more flight over the gaps).
4. D1 r250: fewer throws than r0 and a lower kill rate.

The smoke world (`smoke-1`, build a, red outbound stands, class 50): L3 no
throw; L1 one pull predicted lethal at 1,700 ms, native hit at (269, −112,
128) against the modelled (270, −115, 128), runner survived and passed. That
is where expectation 3 comes from.

## Results

48 worlds an arm (24 cases × classes 50 and 150). `analyse.py` in the output
directory pairs every arm against the control.

| Arm | Pass | Stall | Death before pickup | Carrier dead after pickup | Throws (pull/push) | Native hits | Pit kills |
|---|---|---|---|---|---|---|---|
| Control | 48 | 0 | 0 | 0 | - | - | - |
| D0 body | 46 | 2 | 0 | 0 | 0 | 0 | 0 |
| D1 r0 | 44 | 4 | 0 | 2 | 31 (27/4) | 31 | 2 |
| D1 r250 | 45 | 3 | 0 | 2 | 31 (27/4) | 18 (13 missed the stale aim) | 2 |

Identity: 24 of 24 control cases identical on the defender build (the
control was reused, not re-run).

**1. The throws almost never kill the runner.** Of 31 native hits with
perfect reaction, 2 became pit deaths, both on the carrier leaving the stand
after pickup; none before pickup in any arm. Half the throws (16 of 31) are
on the carrier during `--linger`. The throw model is exact (the native
velocity matches the replayed rule), and an offline brush sweep of the
natively applied velocity agrees with the engine sweep: passive, the runner
falls into the pit. It lands instead on the stairs or ledge 20-50 u off the
passive line, 125-550 ms after the hit, with movement input on every
airborne frame. That is what JKA's air control allows (250 u/s² at
wishspeed 250, about 25 u in 0.45 s), and the runner's own replanning uses
it without knowing about the defender. Expectation 3 held in direction but
not size: not "fewer than half", 2 in 31.

**2. The body blocks the flag, and the runner never goes around.** Every
stall (9 worlds across the three arms) is the same: the runner stands
frozen 52-62 u from the defender on its approach line to the flag, no input,
until the bound. Its forecast refuses the route through the body (the
corridor refusal lands on the defender's position), recovery reports
`floor_startsolid` or `hold` with `decline=corridor`, and no further plan is
made. The body alone does it (D0: blue respawn-4 at L2 and L3, class 150);
a pull makes it likelier by putting the runner on an approach that passes
through the post (D1 r0: both stands' respawn-4 at L2 and L3, class 150).
Expectation 2 failed: the body is not "little change".

**3. Reaction delay.** 250 ms changes which throws land (fewer hits) but
not the kill count; the pit kills in both arms are the carrier at the stand.

## What it means

- The dominant weakness the defender exposed is not pits but **bodies**: the
  movement stack treats a live player as static solid and has no response
  when one sits on the only approach. Humans body-block stands; this would
  stall a real runner permanently.
- The defender's lethality test is wrong for an active target. "Passive
  flight ends in the pit" over-calls; an appropriate defender throws when
  **no** air-control response (passive plus full air acceleration in every
  direction) reaches floor. That is the next defender change, as a new mode
  so mode 1 stays measured.
- The runner's survival of mid-route throws is real but unplanned: it lands
  wherever its replanning happens to steer. Whether it should route to keep
  distance from pit edges near a defender is a later question; first it has
  to get past the body.

## Round 2: through the body, and an honest lethality test (owner, 2026-09-27)

Owner: agreed on both next steps; the runner already has saber melee, so
through or around the body are both options.

- **Through:** the existing pressed-carrier melee (`bot_carrierMelee`, owner
  2026-09-10) swings at a live enemy within 48 u when the runner is stopped.
  The stalls stop at 52-62 u, so `bot_carrierMeleeRange` (default 48,
  unchanged) is set to 96 in these arms. The runner's saber kills the
  holstered defender; a killed defender no longer takes its post back (it
  respawns where the game puts it and throws from there).
- **Mode 2:** a throw is lethal only when the passive flight and every one of
  eight held air-input directions (PM_Accelerate's air rule, pm_airaccelerate
  1) end in the hurt volume. Mode 1 now logs the same escape count.

Build `defender-build-20260927-c`. Arms at classes 50 and 150: identity (no
defender, must match the control), M-D0 (melee, body only), M-D1 (melee,
mode 1, r0), M-D2 (melee, mode 2, r0).

Smoke (`smoke-melee`, the D0 stall case blue_out_L2_respawn4 at class 150):
the runner swung from 62 u, killed the defender, and passed in 12,650 ms
(control 10,000 ms; stalled without melee).

### Expectations before looking (Claude, 2026-09-27)

1. Identity: 24 of 24 identical.
2. M-D0 and M-D1: no stalls; the stall cases pass 2-4 s slower than the
   control. Other cases unchanged except where the runner passes within
   96 u of the defender while stopped.
3. Mode 1's logged escapes: most throws have escapes > 0 (the 29 survivals).
4. M-D2: far fewer throws than mode 1 (a handful at most), and most of those
   that land kill.

### Round 2 results

Identity on build c: 24 of 24 identical to the control. 48 worlds an arm.

| Arm | Pass | Stall | Carrier dead after pickup | Throws | Pit kills | Defender killed |
|---|---|---|---|---|---|---|
| M-D0 (melee, body) | 48 | 0 | 0 | 0 | 0 | 2 |
| M-D1 (melee, mode 1) | 48 | 0 | 2 | 31 | 2 | 4 |
| M-D2 (melee, mode 2) | 48 | 0 | 2 | 2 | 2 | 2 |

**Through works.** Every round-1 stall passes: the runner closes from
52-62 u swinging, kills the holstered defender and takes the flag 1.85-3.2 s
behind the undefended control (M-D0 blue respawn-4 L2/L3 +1,850 ms; M-D1
the four stall cases +2,450 to +3,200 ms). Elsewhere times are unchanged
(M-D0 and M-D2 median delta 0, 36 of 48 identical).

**Mode 1's throws were escapable.** Every mode-1 throw that the runner
survived had 2-7 of 8 air-input directions reaching floor (most 5-7).
Expectation 3 held.

**Mode 2 throws rarely and kills every time.** Two throws in 48 worlds, exactly
the two mode-1 kills: red outbound respawn-5 at
L2 and L3, class 150, a pull 800 ms after pickup on the carrier leaving the
blue stand at 780 u/s, 472 u from the post, zero escapes. Both fell. From
the stand post, nothing on the outbound approach is ever inescapable; the
one inescapable moment is the fast carrier leaving the stand over the
edge. Expectation 4 held (a handful, all kills).

## Where this leaves it

- The body problem is solved by going through, at a cost of about 2-3 s a
  blocked approach. Going around is untested.
- A correct pit defender at the stand kills exactly one thing: the
  departing carrier in the first second after pickup, on one start. That is
  a runner decision (the exit line and speed off the stand), not luck.
- A defender that only throws from the stand post covers little. The pilot
  says human base throws reach a median 1,520 u from their stand; a
  defender that moves to meet the runner is the next pressure to try.

## Round 3: a chaser on the return route (owner, 2026-09-28)

Owner: reconfigure the pull bot as a chaser, to see how the runner's return
route is vulnerable; and is the runner using absorb? **No:** its loadout
(`7-1-033000000000000110`) is levitation 3, Speed 3, saber 1/1; absorb 0,
and no push or pull, so it can neither absorb nor counter a throw.

`bot_defenderChase 1` (`--defender-chase`): the defender holds its post until
the runner carries its flag, then walks the runner's own movement graph
toward where it last saw the carrier, throwing by the same rule and aiming
only on the throw frame. It **walks** (250 u/s against a carrier at
500-700): the hop searches stay the runner's, so the chaser learns nothing
into the shared graph. Each throw records whether the runner was carrying.
The bench keeps only the runner's `[MOVE]` events when a defender is present.

Build `defender-build-20260927-e`. Return cases only (`--filter _ret_`, no
twin skipping: 18 a class), classes 50 and 150: control (no defender), H1
(holding, mode 1), C1 (chasing, mode 1), C2 (chasing, mode 2); defended arms
with melee range 96.

Smoke (build d, red return L2 stand and respawn-5, C1): chased from the
post, lost the carrier, threw only after the capture (the runner going out
again during `--linger`); both passed.

### Expectations before looking (Claude, 2026-09-28)

1. Throws while carrying come only in the first second or two of the stand
   starts, where the carrier leaves the chaser's post; respawn starts (in
   the defender's base) give a few more. A walking chaser falls out of
   throw range soon after.
2. C1 kills no more carriers than H1; C2 kills fewer throws but every one.
3. No stalls (melee).

### Round 3 results

18 return cases a class, classes 50 and 150 (36 worlds an arm). Control 36
of 36. Two holding-arm worlds (H1 class 50, blue return L1 respawn-4 and
respawn-5) are incomplete: every attempt's deferred start came after the
7,000 ms window (world timing, not an outcome; memory was 9.7 GB free).

| Arm | Pass | Carrier deaths | Throws at the carrier (escapes) | Carry time within 1,000 u of the defender |
|---|---|---|---|---|
| H1 holding, mode 1 | 34 (+2 incomplete) | 0 | 11 (4-7) | median 28% |
| C1 chasing, mode 1 | 36 | 0 | 6 (3-7) | median 3% |
| C2 chasing, mode 2 | 36 | 0 | 0 | median 16% |

- **No carrier died on the return in any arm.** Every throw at a carrier
  left it 3-7 of 8 escape directions, and it landed.
- **The walking chaser is a weak probe.** It walks at 250 u/s against a
  carrier at 430-570 (median), reaches the carrier's last-seen point and
  stops (movement owner `none` for about half of a respawn chase). It keeps
  the carrier in range less than a defender that holds the post, which the
  route passes back by. Expectation 1 held; expectation 2 (C1 no more kills
  than H1) held trivially at zero.

### Round 3b: the route itself (shadow tails)

Build `defender-build-20260927-f`, `bot_defenderShadow 1` with a body-only
defender and melee, same 36 return worlds, all passed. Every carrying frame,
a hypothetical thrower on the carrier's own track 250, 500 and 1,000 ms
behind (line of sight required, within 1,000 u) is judged: is a pull or push
passively lethal, and is it inescapable (no air-input direction reaches
floor)?

| Tail lag | Frames judged | Passive-lethal | Inescapable | Windows |
|---|---|---|---|---|
| 250 ms | 18,556 | 454 (2.4%) | 6 | 3 |
| 500 ms | 18,182 | 302 (1.7%) | 0 | 0 |
| 1,000 ms | 16,959 | 195 (1.1%) | 1 | 1 |

Over 472.9 s of carrying, a perfect tail gets **7 inescapable frames in 4
windows**, all pushes, all on level-1 carries (no Speed, about 330 u/s):

- blue and red return L1 respawn-5, class 50: the mirror-image hop at
  (1569, 2587, 1420) / (1053, 1572, 1420), 4.5 s into the carry, airborne
  (vz +141), with the tail 110 u behind.
- red return L1 respawn-4, class 150: (712, 1439, 1837) and 300 ms later
  (642, 1371, 1872), 8.3-8.6 s in, tails 87 u and 380 u behind.

**The return route is geometrically safe against throws.** A thrower
anywhere on its tail can knock it toward a pit about 1-2% of the time, and
the carrier's air control reaches floor from almost all of those. The
inescapable moments are a few mid-hop frames on the slowest (L1) carries
with a chaser practically on its heels. The one place a thrower did kill
(round 2) is the carrier leaving the stand at speed right after a pickup;
the return cases here start at rest on the stand, so they do not contain
it.

Caveat: "escapable" assumes the runner holds the right air input at once.
The runner does not know throws exist; it survived every escapable hit
measured so far only because its replanning steers onto the nearest floor.
