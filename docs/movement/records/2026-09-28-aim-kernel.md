# Aim kernel and humanized aim, 2026-09-28

Owner, 2026-09-28: humanized aiming, "the 47% or in general", with
competency across the most used weapons; Bot2's existing aim code as a
starting point. The human side is measured in the
[human aim study](2026-09-28-human-aim-study.md): 120 demos, 31,617 fires.

Build `aim-build-20260928-f`. Worlds and the analysis are in
`.codex-research/aim-20260928-a/`: `exact.sh`, `human.sh`, `competent.sh`,
`factors.sh`, `competent-f.sh`, and `analyse.py`, which gives hits per shot
by range band against the study's table.

## What was built

**`bot2_aim.c` / `.h`, a shared kernel** (in `CMakeLists.txt`). The target
is always the caller's perceived state; nothing here reads the target
entity.

- **`Bot2_AimSolve`** starts from `Bot2_GetLeadOrigin` (`ai_bot2_combat.c`):
  the same projectile speeds, an iterated lead, a floor-clamped fall, and a
  splash burst at the floor.
  - A gravity projectile (repeater alt, flechette alt) is solved as a
    low-arc launch angle, with the flechette alt's built-in 10° lift, not a
    hand-set height boost.
  - `BOT2_AIM_SYNC` marks a shot whose target is on the floor when the shot
    arrives: the old combat code's "synced".
- **`Bot2_AimHumanPoint`** is the study's human aim point: the current
  centre with no lead, a per-mode pitch offset, and the disruptor aimed
  75 ms behind.
- **`Bot2_AimHumanError`** is the study's error: base(mode) + k × the
  target's angular speed, as a per-axis standard deviation.
- **`Bot2_AimHumanStep`** is the humanized view:
  - pursuit, matching the desired angle's own motion;
  - a first-order lag on the remaining gap;
  - a turn-rate cap;
  - an Ornstein-Uhlenbeck error from a seeded hash, so pinned worlds replay
    and the game's random stream is untouched.

**The defender** (`bot2_defender.c`) aims through the kernel.

- **Modes:** `bot_defenderAlt`, and a charged pistol with
  `bot_defenderChargeMs`.
- **Styles** (`bot_aimStyle`):
  - **0:** exact lead with the view cvars.
  - **1:** human point, error, 300 ms lag, 500°/s cap, 1.2 s engage delay
    and 1.15× refire pacing.
  - **2, competent:** the kernel's lead through the same humanized view;
    splash and lobbed shots only when synced.
- **While moving,** it flicks onto the target 250 ms before the weapon is
  ready. A shot is logged when ammo is spent or the refire time starts; the
  pistol spends no ammo.

## Findings

1. **The human process copied literally fails against this runner**
   (style 1): 3-7% hits for splash weapons, no kills. With no lead, a
   900 u/s rocket lands about 300 u behind a runner at 500-600 u/s, and the
   engage delay pushed most shots past 1,000 u. The study's "no lead" comes
   from humans' own targets and shot choices; it is not a rule that holds
   here.
2. **A first-order view lag cannot track.** A target crossing at ω stays
   ω × lag behind: 18° at 60°/s with 300 ms lag. Style 2 at zero error hit
   7% with the rocket (build `e`).
   - **Pursuit fixed it (build `f`):** 50% overall, 93/29/21/22% by band.
   - **Factor arms, rocket, zero error:** exact 34% of 260 shots (14
     kills); view only 42%; competent 50%; competent without the engage
     delay 44%.
   - The competent style fires fewer, better shots.
3. **Competent style against the human table** (build `f`, 42 worlds a
   mode; bot hits per shot vs human, bands <300 / 300-600 / 600-1,000 /
   >1,000 u):

| Mode | Zero error | Human error (×1) | Humans |
|---|---|---|---|
| rocket | 90 / 32 / 17 / 21 (48% of 112) | 68 / 5 / 19 / 0 (28% of 115) | 79 / 53 / 38 / 37 |
| repeater alt | 67 / 21 / 31 / 0 (31%) | 58 / 19 / 11 / 0 (25%) | 77 / 55 / 42 / 37 |
| flechette alt | 6 / 2 / 7 / 7 (5%) | 11 / 12 / 13 / 2 (10%) | 32 / 19 / 12 / 16 |
| concussion | 98 / 86 / 34 / 38 (66%) | 89 / 58 / 26 / 32 (52%) | 77 / 69 / 66 / 45 |
| charged pistol | - | 3 / 0 / 0 / 0 (1%) | 27 / 12 / 7 / 3 |
| repeater | - | 1 / 0 / 0 / 0 (1% of 1,421) | 16 / 4 / 2 / 2 |
| flechette | - | 2 / 1 / 3 / 2 (2%) | 42 / 34 / 15 / 16 |
| disruptor | - | 6 / 0 / 7 / 1 (3%) | 28 / 27 / 27 / 26 |

- **Close range:** the bot matches or beats humans.
- **Mid and long range with splash weapons:** it falls short even with
  zero error. The runner is a harder target than humans' average: it is
  always strafing at 500-600 u/s and changes side every hop.
- **Human-sized error** takes the rocket and repeater alt well below the
  human rates at 300 u and beyond.
- **Bolt weapons are blocked, not missed.** The pistol, repeater, flechette
  and disruptor hit the runner 0-7%. The runner holds a lit saber, and the
  game lets a saber block every projectile except rocket, thermal, mines,
  DEMP2, repeater alt, flechette alt and concussion (`g_missile.c`).
  Humans' rates for these weapons come mostly from targets not blocking.
  Calibrating them needs a target that is not blocking.
- **The flechette alt** is weak with any aim: its balls fly at a random
  700-1,400 u/s and burst on a 1.5-3.5 s timer.

## Open (owner)

- **A calibration target for the bolt weapons:** the runner with its saber
  holstered, as a declared fixture setting. That separates aim from blocks.
- **Error scale:** the study's full error (×1) against a hard runner, or a
  smaller scale chosen to match human hit rates on this target.
- **Shot selection beyond sync:** humans fire about one shot per
  engagement. Firing only in the bands and moments where the weapon hits
  is the next lever for mid range.
- **Weapon choice by range and target:** concussion and rocket up close,
  concussion at range, no bolts against a blocking saber. This is the
  defender's first real combat decision.

## Splash weapons: shot selection and weapon choice (owner: bolt weapons ignored for now)

Builds `aim-build-20260928-g` to `-i`. Worlds are in the same directory
(`collect.sh`, `validate.sh`, `validate2.sh`, `fulldef.sh`; `shots.py` and
`validate.py` analyse them).

**Which shots hit.** 1,381 fire-at-will shots (competent style, human
error, classes 50 and 150). Each `FIRE` line now carries the shot's flight
time, sync flag, and the target's speed, vertical speed, grounded state,
time to land and angular speed.

- **Flight time decides.** Rocket: 85% of shots under 200 ms of flight
  hit, 44% at 200-350 ms, 17% at 350-500 ms, 1% beyond 700 ms, and half the
  shots were in that last band. The repeater alt is the same.
- **The sync flag separates nothing** (rocket 22% vs 23%). A strafing
  runner nearly always lands before the shot arrives.
- **Slow targets are hit more** (rocket 44% under 300 u/s, 2% over
  600 u/s).
- **The flechette alt** hits 3-5% in every bin, so it was left out.
- **One rule fits all three good weapons:** the reach ratio, i.e. the
  target's speed × the shot's flight time ÷ the splash radius (rocket 160,
  repeater alt 128, concussion 200).
  - Shots with a ratio under 1 hit 52% (rocket, 41% of shots kept), 50%
    (repeater alt, 26%) and 56% (concussion, 72%).
  - With zero error: 88%, 77% and 69%.

**Built.**
- **`bot_aimGate 2`** holds a splash shot while the ratio is over
  `bot_aimReach` (default 1).
- **The kernel** gains `Bot2_AimSplashRadius`, `Bot2_AimDamage`, and
  `Bot2_AimHitChance`: the measured hit chance by ratio band, against this
  runner.
- **`bot_defenderWeapons "11,8a,15"`** carries several weapons and picks
  by expected damage a second: hit chance × damage ÷ refire time. A switch
  must be 50% better, and no switch comes within 2 s of the last.

**Validation on classes 25 and 100** (not the classes the table was fitted
on). Human error, no throws or pulls, 48 worlds an arm.

| Arm | Hits per shot | Shots | By band (<300 / 300-600 / 600-1,000 / >1,000) | Carrier kills |
|---|---|---|---|---|
| rocket, fire at will | 29% | 235 | 66 / 18 / 9 / 5 | 4 |
| rocket, reach gate | 59% | 102 | 72 / 37 / 0 / - | 3 |
| repeater alt, gate | 56% | 135 | 59 / 53 / 0 / - | 0 |
| concussion, gate | 55% | 256 | 85 / 39 / 44 / 24 | 2 |
| mixed, first choice (build `h`) | 49% | 183 | 83 / 46 / 44 / 32 | 0 (215 switches) |
| mixed, hysteresis (build `i`) | 59% | 243 | 80 / 54 / 41 / 40 | 1 (52 switches) |

- **The gate** doubles the hit rate on about half the shots, and all three
  weapons clear the humans' 47%.
- **The mixed loadout** fires the concussion almost always: 239 of 243
  shots, 143 hits. Its 3,000 u/s shot keeps the ratio small at every
  range. When the rocket is chosen, the runner is usually out of the
  rocket's window by the time the weapon is raised.

**The full base defender with this aim.** Guard, carrier pull at
350-700 u, lethal throws, the competent gated aim at human error, on the
worlds of `basedef-20260928-b`:

| Defender | Hits per shot | Carrier kills, returns (36) | Carrier kills, pickups (48) |
|---|---|---|---|
| exact aim, rocket, fire at will (`basedef-20260928-b`) | 26-30% | 9 | 19 |
| competent, human error, rocket | 58% (55 shots, returns) | 1 | 3 |
| competent, human error, mixed | 59-61% | 0 | 4 |

The humanized defender aims like a good human but shoots far less: the
humans' 1.2 s engage delay and the gate take away the first-second shots
that made the exact defender's kills (its return kills came 1.2 s into the
carry). Humans' first shot at a thief comes a median 1.25 s after the
steal, with a p25 of 450 ms.

## Open (owner)

- **How quick and how willing the defender is:**
  - the engage delay: the human median of 1.2 s, or a sharp human's
    450 ms;
  - the reach threshold: 1 favours accuracy; higher favours volume and
    kills.
- **Weapon choice:** to use the rocket as a real option, the choice needs
  to count the raise time, or pick a weapon before the engagement rather
  than during it.

## Firing before the grab (owner: "we should be firing even before the offense grabs our flag")

Build `aim-build-20260928-j` adds a diagnostic: every frame before the pickup
is tallied by why the defender did not fire, printed at the pickup as
`[DEFENDER] PRESHOT`. Kejim outbound cases, class 50, full defender, rocket,
competent style, human error, reach gate. Worlds are
`.codex-research/aim-20260928-a/preshot-*`.

- **Nothing stops a visible shot:** the engage delay counts from the
  attacker first in sight and range, and firing is not tied to the steal.
- **The attacker is rarely visible.** With the 1.2 s engage delay:
  - 80% of pre-pickup frames: not visible (10.7 s a world);
  - engage delay: 7% (0.9 s);
  - out of range: 7% (0.9 s);
  - line of fire blocked: 3%;
  - weapon not ready: 2%;
  - fired: 5 frames in 19 pickups.
- **The approach is mostly hidden.** The runner is within 1,500 u of the
  defender for a median 4.5 s before the pickup, but visible from the post
  for about 2 s.
- **A shorter engage delay helps a little:**

| Engage delay | Pre-pickup shots | Hits | Attackers killed before the grab |
|---|---|---|---|
| 1,200 ms (human median) | 8 | 5 | 0 |
| 450 ms (human p25) | 14 | 12 | 0 |
| none | 14 | 10 | 1 |

- **Pre-pickup shots hit very well** (10-12 of 14): the attacker slows at
  the stand.
- **What limits the defender here** is its view of the approach, not its
  willingness to fire.

**Open (owner):**
- **Posts chosen for sightlines onto the approach routes.** The study's
  posts come from 4v4 base players, who also have teammates' calls.
- **Awareness by sound:** humans hear an attacker coming (footsteps, jumps,
  Force Speed). This is within the knowledge boundary if modelled as
  hearing. It would let the defender pre-aim and fire as the attacker
  comes into view.
- **A shorter engage delay for an attacker already known to be coming.**

## Where the defender stands (owner: a lone 2v2 defender has a better view)

**The approach view.** Kejim Approach Sightlines
is built from the `SIGHT` log (`bot_defenderSightLog`, build
`aim-build-20260928-l`; data `sightview_data.py`).

- The defender holds post 0 on its own stand the whole approach.
- From respawn 4 the attacker comes through mid and is seen 3-6 s before
  the grab.
- From the stand and respawn 5, at every level, it comes up the lower route
  and surfaces 400-600 u east of the stand, 1 s before the grab.
- Counting the head as visible adds only 0.2-0.8 s.

**2v2 positions** (`tools/defender_approach_posts.py`: 46 Kejim 2v2 demos,
the study's defender rule, red folded onto blue by point symmetry).
- **With an enemy within 1,500 u of the stand:**
  - 55% on the stand: red (219, -1066), blue (2397, 5218);
  - 36% forward toward mid: red (295, -25), blue (2321, 4177).
- **In the 3 s before a steal:** 60% on the stand, 26% forward.
- A lone defender stands where the bot does about half the time; the
  forward post is the 2v2 addition.

**The posts tested** (the 18 outbound cases, one post each; worlds
`sight-forward-*`, `sight-entry-*`; `posts_compare.py`):

| Post | Seen, of time within 1,500 u | First seen before the grab (median) | Shots before the grab | Attackers stopped | Carriers killed |
|---|---|---|---|---|---|
| stand (post 0) | 50% | 1.5 s | 5 | 0 | 1 |
| 2v2 forward: red (194, -143, 1797) | 68% | 5.5 s | 3 | 1 | 0 |
| entry spot: red (631, -1335, 1841), a 6% 2v2 cluster | 76% | 4.2 s | 24 | 2 | 0 |

**What the pre-grab tally says:**
- **Forward post:** the attacker is visible, but the shot's aim point is
  hidden for 2.4 s a world. The rocket aims at the floor under the
  attacker, which a ledge hides from there. A direct body shot as the
  fallback would use that time.
- **Entry spot:** mostly out of range (3.1 s), which is not a problem.
- **Off the stand, the defender loses the carrier after the grab:** no
  carrier kills from either alternative post.

## 2v2 guard posts (owner: go ahead), 2026-09-29

Build `aim-build-20260929-a`; worlds and `compare.py` in
`.codex-research/aim-20260929-a/`.

**Built.**
- `posts.json` gains `guard_posts_2v2` (bench `--defender-posts 2v2`): the
  entry spot where the lower route surfaces, the stand, and the forward
  post.
- The entry spot is post 0, held while the attacker is unseen. Once the
  attacker is seen near, the guard takes the post nearest its line to the
  stand, as before.
- **Direct-shot fallback** (`Bot2_AimSolveDirect`): a splash shot whose
  floor point is hidden aims at the body with the same lead.

**Result.** Full defender, rocket, competent aim at human error; Kejim
pickups (48 worlds) and returns (36), classes 50 and 150:

| | Study posts | 2v2 posts |
|---|---|---|
| Approach seen, of time within 1,500 u | 50% | 80% |
| First seen before the grab (median) | 1.6 s | 4.2 s |
| Shots before the grab (hits) | 10 (8) | 15 (10) |
| Attackers stopped before the grab | 0 | 1 |
| Carriers killed, pickups / returns | 3 / 1 | 2 / 1 |

- The 2v2 posts see the attacker much sooner. Shots before the grab rise
  by half, and they hit.
- Kills barely move: a rocket hit does not kill a full-health attacker.
  10 hits on 36 approaches stopped one.
- **Frames still lost before the grab:**
  - out of range while visible: 3.1 s a world (the shooting range is
    1,500 u);
  - blocked shot lines: 1.6 s, even with the direct fallback;
  - moving between posts takes the defender off the entry spot. The
    entry-spot-only arm on build `l` fired 24 shots and stopped 2.
