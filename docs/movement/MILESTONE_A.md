# Milestone A: the runner fights back (plan, 2026-10-02)

**Status (2026-10-02): passed (owner).** Steps 0-2 built. Protect is the runner's default (`bot_runnerProtect 1`): captures
4.31 → 6.44 and weapon deaths 33 → 21 a world; absorb is off by default (it held the slot protect needs). Self-kill
after a capture is on in play. Records: [absorb](records/2026-10-02-runner-absorb-v1.md),
[protect](records/2026-10-02-runner-protect-v1.md), [capture self-kill](records/2026-10-02-runner-capkill.md).

## Outcome

16 laps worlds an arm throughout; numbers per ten-minute world.

- **Step 0, loadout:** absorb 3 + protect 3 (1d29e520). Kejim runner floor 24/24 at classes 150 and 50 in both arms,
  48 of 48 completion times identical with and without the new powers
  ([absorb record](records/2026-10-02-runner-absorb-v1.md)).
- **Step 1, absorb (`bot_runnerAbsorb`):** throw pits 0.62 → 0.12 but captures 4.50 → 1.38 (v1) and 0.81 (v3). The
  cause was the forecast refusing every rollout while absorb was up, fixed in 947c39e4; with the fix mode 1 ran 2.94
  captures and 34.5 runner deaths against 4.50 / 39.5, and mode 2 (raise near the flag, drop over 50 Force) 3.31 /
  39.1. The owner first accepted mode 1 as the default (merged 8fd60ddf); step 2's comparison then rejected both
  modes ([absorb record](records/2026-10-02-runner-absorb-v1.md)).
- **Step 1b, self-kill after a capture (`bot_runnerCapKill`):** capture to next pickup 35.0 → 15.7 s median, captures
  3.81 / 3.50 inside the spread; on in play, 0 on the bench ([record](records/2026-10-02-runner-capkill.md)).
- **Step 2, protect (`bot_runnerProtect`):** beside absorb it was up 4% of the time and changed nothing. Absorb off,
  protect on: captures 4.31 → 6.44 and weapon deaths 33.3 → 20.8 against both off, at the cost of carrying without
  Speed 4% → 35% and throw pits 0.06 → 0.94; every absorb arm ran 3.3-3.7 captures. Default protect on, absorb off
  (merged 407e7827, defaults 872292a8) ([protect record](records/2026-10-02-runner-protect-v1.md)).

## Open after the pass

- The defenders' answer to protect (situation 5: splash, knockback toward pits, Force denial); the owner tries it in
  play first.
- A protect drop-when-clear rule, to recover the regeneration a 20 s protect blocks.
- A transfer check of these behaviours on ctf_dantooine_v3, the map in play: every Force and combat decision here
  was measured on Kejim only.
- The team situations (1, 3, 4, 6), deferred until a teammate bot exists; the MoveGuest seam
  ([record](records/2026-09-28-strafing-chaser.md)) makes one cheaper.
- The runner Force policy review fixes of 2026-10-02 are fixed but unmeasured: re-run the laps arms before they count.

## The plan as written (2026-10-02)

Owner order (2026-10-02): write the plan, start with **absorb at pit edges**. This is the roadmap's milestone A
([PLAN.md](PLAN.md#roadmap-owner-2026-09-27)) narrowed to what a lone runner can do. Criteria past the pilot were not
fixed in advance; the acceptance below is proposed for the owner to confirm.

## Scope

Kejim, Runner_v2 alone (blue) against base D and chase D (red) on the current defender defaults
([record](records/2026-10-01-errand-falls-library-test.md)). Of the six candidate situations from the pilot
([community discussions](records/2026-09-27-community-discussions.md#candidate-named-situations-for-milestone-a)):

| situation | 1v2 fixture | why |
|---|---|---|
| 2. absorb at a pit edge with an enemy in push range, protect otherwise | **this plan** | needs no teammate |
| 1. low-Force carrier runs to its base's reach and calls `te` | deferred | needs a teammate to answer the call |
| 3. spent carrier hands off by self-kill | deferred | needs a fresh teammate |
| 4. base with the carrier home: TE, sk or clear the camper | deferred | a defender-side team play |
| 5. chase meeting a protecting carrier | follows step 2 | the defenders' answer to protect |
| 6. the camper's trade | deferred | needs a camper |

## Where the runner stands (baseline)

New defender defaults, errands hopped, `bot_defender 2`, 16 laps worlds of ten game minutes
(`.codex-research/sk-20261002-a/sk0-*`):

- **Captures 2.81** per ten minutes (the owner allowed 1 in 3.6 minutes, i.e. 2.78). **Runner deaths 39.8**.
- **Deaths by cause:** repeater alt splash 29%, trip mine 17%, rocket 16%, repeater alt 14%, flechette alt splash 12%,
  **falling 6% (2.3 a world)**, rocket splash 5%. Weapons kill the runner; pits rarely do.
- **Throws:** about 11 hits a world, nearly all pulls (10.4), 63% on an airborne runner, which cannot counter today
  ([throw counters](records/2026-09-29-runner-throw-counters.md)). Tracked outcomes a world: death 4.7,
  pit 0.5, survived 5.4 (`[DEFENDER] OUTCOME`).

So absorb addresses about 5 of 40 deaths a world: the pulls that set up a kill or a pit. The larger term is weapon
damage, which is protect's (step 2). Both are one decision because the game runs only one of them at a time.

## Mechanics (this codebase)

- **Loadout.** The runner has neither power: `7-1-033330000000000110` (jump, speed, push, pull 3; saber 1/1;
  `tools/fixtures/strafe_play/kejim_runner.bot`). Costs are cumulative per level (`bgForcePowerCost`, bg_misc.c): it
  spends 42 of rank 7's 100 points; absorb 3 (+10) and protect 3 (+15) fit, 67 of 100.
- **Absorb** (`ForceAbsorb`, `WP_AbsorbConversion`, w_force.c): activation 10 Force at level 3 (`forcePowerNeeded`,
  bg_pmove.c), then 1 Force every 600 ms while up, against regeneration of 1 per `g_forceRegenTime` (200 ms in play).
  An absorbed push or pull loses absorb's level (level 3 against push 3: no push at all,
  `pushPower = 256 * max(0, level - absorb)`) and refunds a third of the thrower's spend times absorb's level. **No
  ground check**: it works in the air, where the counter does not. It stops protect and rage; it cannot be dropped for
  1,500 ms after it is raised (`forceAllowDeactivateTime`). Only push, pull, grip, lightning and drain are absorbed.
- **Both end on their own** (`WP_ForcePowerStart`): 20 s after they are raised, when Force reaches 0, or when the
  other is raised. Pressing absorb again while it is up and past its 1,500 ms switches it **off**.
- **Protect** (g_combat.c, `FP_PROTECT`): at level 3, health takes 20% of a hit and Force pays a quarter of it (per
  hit, capped at 400); what Force cannot pay comes back onto health. Activation 10 at level 3. It stops absorb.

## Step 0: loadout and identity (no behaviour)

Give the runner absorb 3 and protect 3 in the runner fixtures (the `.bot`/`.jkb` pair the tools stage). Nothing uses
them yet. Check: the Kejim runner floor (36/36, zero deaths, decisions identical on the deterministic pinned classes)
and 16 laps worlds against the baseline (captures, deaths by cause inside the spread). Owner decision: absorb 3 and
protect 3, or another split (heal 3 also fits: 79 of 100).

## Step 1: absorb at pit edges

*Status: measured, rejected as the default; mode 2 also rejected ([Outcome](#outcome)).*

**Decision, runner-owned** (a Bot2 kernel for the exposure test, the runner's Force command issued where the movement
already issues `GENCMD_FORCE_SPEED`):

1. **Threats are perceived, not known.** A defender counts when the runner has seen it within the defenders' own
   reaction model (visible now, or last seen under `bot_threatReactMs`-style latency) and it is within push/pull reach.
   The runner cannot see an enemy's Force; it assumes a throw is available unless it saw one within its cooldown.
2. **Exposure is forecast, not guessed.** For each threat, would a pull toward it or a push away from it, applied now
   (and through a committed flight), leave the runner in a lethal fall or the hurt volume? The defenders already
   predict exactly this from their side (lethal-throw prediction, escapes, `Move_DeclareKnock`, knock recovery
   rollouts); the runner asks the same question of its own state, with its own recovery counted.
3. **Raise absorb** when any perceived threat makes the current state, or the committed flight, exposed, absorb is not
   already up, and Force is at least the activation plus the route's reserve. **Activation only** (owner,
   2026-10-02): the runner never lowers it; it ends after 20 s, at Force 0, or when step 2 raises protect. The press
   must be skipped while absorb is up, or it would switch it off. A deactivation rule is added only if the
   measurements show absorb's uptime starving protect or the Speed purchases.
4. **Force is one pool.** The route planner's spend policy (Speed purchases, `SpendPolicy`) must see what absorb holds
   back; a raised absorb that makes the next Speed purchase fail is a cost, not free.

**Measurement.**
- **Throw replays (new fixture, cheap and deterministic).** From the baseline worlds, every throw whose tracked outcome
  was death or pit, started at the throw frame with both sides' state restored, as the knock replays do for defender
  falls ([knock replays](records/2026-10-01-knock-replays.md); `defense_laps.py --scenario`). Arms: absorb off / on.
  Read: pit and death outcomes, whether absorb was up when the throw landed.
- **Laps worlds**, 16 an arm: captures, runner deaths by cause, throw outcomes (death / pit / survived), absorb uptime
  and Force spent on it, Speed purchases refused for Force.
- **Runner floor** (36/36) to show the movement is unchanged where nothing threatens.

**Proposed acceptance (owner to confirm).** Throw-attributed runner deaths (OUTCOME death + pit) at least halved in
the laps arms; no rise in any other cause of death; captures not lower; Speed purchases refused for Force not raised
by more than a stated margin; runner floor 36/36 with identical decisions on the pinned classes.

## Step 1b: self-kill after a capture (owner, 2026-10-02)

`bot_runnerCapKill 1` (ai_bot2.c, default 0): a runner that has just captured kills itself when its Force is under
`bot_runnerCapKillForce` (default 100), to respawn full rather than set out spent (`[RUNNER] CAPKILL`). Human offense
players do this on their own stand 40% of the time against a 19% baseline and are back in about 1.2 s
([pilot](records/2026-09-27-position-pilot.md#self-kills-and-team-energize-findings-same-day)). Independent of the
loadout; measured first, in laps worlds against the same defaults.

## Step 2: protect when under fire with no pit exposure

The same decision's other branch, and by the baseline the bigger prize (weapons are 94% of runner deaths). Protect
when perceived fire (seen shooters, incoming missiles the threat model already tracks) is expected and step 1 finds no
exposure; absorb wins whenever both apply, and when health is too low to survive the damage even at the edge the
owner's rule picks protect knowingly. Measured the same way; the defenders then need an answer (situation 5: splash,
knockback toward pits, Force denial), which is the next round of defender work.

## Evidence to gather alongside (offline)

How human carriers use absorb and protect near pits: the position pilot answered Force use from the POV's own
snapshots ([pilot](records/2026-09-27-position-pilot.md)). A small study of carriers' absorb/protect toggles against
enemy distance and pit proximity would calibrate the raise and lower rules (observed: activations, positions; inferred:
why). It runs while steps 0-1 are built.

## Owner decisions (resolved)

1. **Loadout:** absorb 3 + protect 3, as proposed (step 0).
2. **A defender's throw:** the runner assumes a defender has one. As built it keeps a soft per-enemy Force estimate,
   biased so that any doubt counts the enemy as able to throw (code comment in `codemp/game/ai_bot2_objective.c`:
   "Bias (owner, 2026-10-02): any doubt raises absorb"); one able threat is enough.
3. **Acceptance:** not fixed numbers. The owner judged the measured arms
   ([absorb](records/2026-10-02-runner-absorb-v1.md), [protect](records/2026-10-02-runner-protect-v1.md),
   [capture self-kill](records/2026-10-02-runner-capkill.md)) and declared the milestone passed on 2026-10-02.
